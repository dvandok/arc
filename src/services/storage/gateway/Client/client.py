from storage.common import hash_uri, catalog_uri, manager_uri, rbyteio_uri, byteio_simple_uri, element_uri, gateway_uri, transfer_uri, externalInfo_uri
from storage.common import parse_metadata, create_metadata, true, false, get_child_nodes, node_to_data, parse_node, parse_to_dict
from storage.xmltree import XMLTree
from xml.dom.minidom import parseString
import arc
import base64

class Client:
    """ Base Client class for sending SOAP messages to services """

    def __init__(self, url, ns, print_xml = False):
        """ The constructor of the Client class.
        
        Client(url, ns, print_xml = false)
        
        url is the URL of the service
        ns contains the namespaces we want to use with each message
        print_xml is for debugging, prints all the SOAP messages to the screen
        """
        import urlparse
        (_, host_port, path, _, _, _) = urlparse.urlparse(url)
        if ':' in host_port:
            host, port = host_port.split(':')
        else:
            host = host_port
            port = 80
        self.host = host
        self.port = port
        self.path = path
        self.ns = ns
        self.url = url
        self.print_xml = print_xml

    def call(self, tree, return_tree_only = False):
        """ Create a SOAP message from an XMLTree and send it to the service.
        
        call(tree, return_tree_only = False)
        
        tree is an XMLTree object containing the content of the request
        return_tree_only indicates that we only need to put the response into an XMLTree
        """
        # create a new PayloadSOAP object with the given namespace
        out = arc.PayloadSOAP(self.ns)
        # add the content of the XMLTree to the XMLNode of the SOAP object
        tree.add_to_node(out)
        if self.print_xml:
            msg = out.GetXML()
            print 'Request:'
            print XMLTree(out).pretty_xml(indent = '    ', prefix = '        #   ')
            print
        # call the service and get back the response, and the HTTP status
        resp, status, reason = self.call_raw(out)
        if self.print_xml:
            print 'Response:'
            try:
                print XMLTree(from_string = resp).pretty_xml(indent = '    ', prefix = '        #   ')
            except:
                print resp
            print
        if return_tree_only:
            # wrap the response into an XMLTree and return only the tree
            return XMLTree(from_string = resp, forget_namespace = True).get_trees('///')[0]
        else:
            return resp, status, reason

    def call_raw(self, outpayload):
        """ Send a POST request with the SOAP XML message.
        
        call_raw(outpayload)
        
        outpayload is an XMLNode with the SOAP message
        """
        # TODO: use the client-side libs of ARCHED
        import httplib
        # create an HTTP connection
        h = httplib.HTTPConnection(self.host, self.port)
        # get the XML from outpayload, and send it as POST
        h.request('POST', self.path, outpayload.GetXML())
        # get the response object
        r = h.getresponse()
        # read the response data
        resp = r.read()
        # return the data and the status
        return resp, r.status, r.reason


class HashClient(Client):

    def __init__(self, url, print_xml = False):
        ns = arc.NS({'hash': hash_uri})
        Client.__init__(self, url, ns, print_xml)

    def get_tree(self, IDs, neededMetadata = []):
        tree = XMLTree(from_tree =
            ('hash:get', [
                ('hash:neededMetadataList', [
                    ('hash:neededMetadataElement', [
                        ('hash:section', section),
                        ('hash:property', property)
                    ]) for section, property in neededMetadata
                ]),
                ('hash:IDs', [
                    ('hash:ID', i) for i in IDs
                ])
            ])
        )
        msg, status, reason = self.call(tree)
        xml = arc.XMLNode(msg)
        hash_prefix = xml.NamespacePrefix(hash_uri)
        rewrite = {
            hash_prefix + ':objects' : 'cat:getResponseList',
            hash_prefix + ':object' : 'cat:getResponseElement',
            hash_prefix + ':ID' : 'cat:GUID',
            hash_prefix + ':metadataList' : 'cat:metadataList',
            hash_prefix + ':metadata' : 'cat:metadata',
            hash_prefix + ':section' : 'cat:section',
            hash_prefix + ':property' : 'cat:property',
            hash_prefix + ':value' : 'cat:value'
        }
        return XMLTree(xml.Child().Child().Child(), rewrite = rewrite)

    def get(self, IDs, neededMetadata = []):
        tree = XMLTree(from_tree =
            ('hash:get', [
                ('hash:neededMetadataList', [
                    ('hash:neededMetadataElement', [
                        ('hash:section', section),
                        ('hash:property', property)
                    ]) for section, property in neededMetadata
                ]),
                ('hash:IDs', [
                    ('hash:ID', i) for i in IDs
                ])
            ])
        )
        msg, status, reason = self.call(tree)
        xml = arc.XMLNode(msg)
        objects = parse_node(xml.Child().Child().Child(), ['ID', 'metadataList'], single = True, string = False)
        return dict([(str(ID), parse_metadata(metadataList)) for ID, metadataList in objects.items()])

    def change(self, changes):
        """ Call the change method of the Hash service.
        
        change(changes)

        'changes' is a dictionary of {changeID : (ID, changeType, section, property, value, conditions)}
            where 'conditions' is a dictionary of {conditionID : (conditionType, section, property, value)}
        """
        tree = XMLTree(from_tree =
            ('hash:change', [
                ('hash:changeRequestList', [
                    ('hash:changeRequestElement', [
                        ('hash:changeID', changeID),
                        ('hash:ID', ID),
                        ('hash:changeType', changeType),
                        ('hash:section', section),
                        ('hash:property', property),
                        ('hash:value', value),
                        ('hash:conditionList', [
                            ('hash:condition', [
                                ('hash:conditionID', conditionID),
                                ('hash:conditionType',conditionType),
                                ('hash:section',conditionSection),
                                ('hash:property',conditionProperty),
                                ('hash:value',conditionValue)
                            ]) for conditionID, (conditionType, conditionSection,
                                        conditionProperty, conditionValue) in conditions.items()
                        ])
                    ]) for changeID, (ID, changeType, section, property, value, conditions) in changes.items()
                ])
            ])
        )
        msg, status, reason = self.call(tree)
        xml = arc.XMLNode(msg)
        return parse_node(xml.Child().Child().Child(), ['changeID', 'success', 'conditionID'])

class CatalogClient(Client):
    
    def __init__(self, url, print_xml = False):
        ns = arc.NS({'cat':catalog_uri})
        Client.__init__(self, url, ns, print_xml)

    def get(self, GUIDs, neededMetadata = []):
        tree = XMLTree(from_tree =
            ('cat:get', [
                ('cat:neededMetadataList', [
                    ('cat:neededMetadataElement', [
                        ('cat:section', section),
                        ('cat:property', property)
                    ]) for section, property in neededMetadata
                ]),
                ('cat:getRequestList', [
                    ('cat:getRequestElement', [
                        ('cat:GUID', i)
                    ]) for i in GUIDs
                ])
            ])
        )
        msg, status, reason = self.call(tree)
        xml = arc.XMLNode(msg)
        elements = parse_node(xml.Child().Child().Child(),
            ['GUID', 'metadataList'], single = True, string = False)
        return dict([(str(GUID), parse_metadata(metadataList))
            for GUID, metadataList in elements.items()])

    def traverseLN(self, requests):
        tree = XMLTree(from_tree =
            ('cat:traverseLN', [
                ('cat:traverseLNRequestList', [
                    ('cat:traverseLNRequestElement', [
                        ('cat:requestID', rID),
                        ('cat:LN', LN)
                    ]) for rID, LN in requests.items()
                ])
            ])
        )
        msg, status, reason = self.call(tree)
        xml = arc.XMLNode(msg)
        list_node = xml.Child().Child().Child()
        list_number = list_node.Size()
        elements = {}
        for i in range(list_number):
            element_node = list_node.Child(i)
            requestID = str(element_node.Get('requestID'))
            traversedlist_node = element_node.Get('traversedList')
            traversedlist_number = traversedlist_node.Size()
            traversedlist = []
            for j in range(traversedlist_number):
                tle_node = traversedlist_node.Child(j)
                traversedlist.append((str(tle_node.Get('LNPart')), str(tle_node.Get('GUID'))))
            wasComplete = str(element_node.Get('wasComplete')) == true
            traversedLN = str(element_node.Get('traversedLN'))
            restLN = str(element_node.Get('restLN'))
            GUID = str(element_node.Get('GUID'))
            metadatalist_node = element_node.Get('metadataList')
            metadata = parse_metadata(metadatalist_node)
            elements[requestID] = (metadata, GUID, traversedLN, restLN, wasComplete, traversedlist)
        return elements

    def new(self, requests):
        tree = XMLTree(from_tree =
            ('cat:new', [
                ('cat:newRequestList', [
                    ('cat:newRequestElement', [
                        ('cat:requestID', requestID),
                        ('cat:metadataList', create_metadata(metadata, 'cat'))
                    ]) for requestID, metadata in requests.items()
                ])
            ])
        )
        response, _, _ = self.call(tree)
        node = arc.XMLNode(response)
        return parse_node(node.Child().Child().Child(), ['requestID', 'GUID', 'success'])

    def modifyMetadata(self, requests):
        tree = XMLTree(from_tree =
            ('cat:modifyMetadata', [
                ('cat:modifyMetadataRequestList', [
                    ('cat:modifyMetadataRequestElement', [
                        ('cat:changeID', changeID),
                        ('cat:GUID', GUID),
                        ('cat:changeType', changeType),
                        ('cat:section', section),
                        ('cat:property', property),
                        ('cat:value', value)
                    ]) for changeID, (GUID, changeType, section, property, value) in requests.items()
                ])
            ])
        )
        response, _, _ = self.call(tree)
        node = arc.XMLNode(response)
        return parse_node(node.Child().Child().Child(), ['changeID', 'success'], True)

    def remove(self, requests):
        tree = XMLTree(from_tree =
            ('cat:remove', [
                ('cat:removeRequestList', [
                    ('cat:removeRequestElement', [
                        ('cat:requestID', requestID),
                        ('cat:GUID', GUID)
                    ]) for requestID, GUID in requests.items()
                ])
            ])
        )
        response, _, _ = self.call(tree)
        node = arc.XMLNode(response)
        return parse_node(node.Child().Child().Child(), ['requestID', 'success'], True)

    def report(self, serviceID, filelist):
        tree = XMLTree(from_tree =
            ('cat:report', [
                ('cat:serviceID', serviceID),
                ('cat:filelist', [
                    ('cat:file', [
                        ('cat:GUID', GUID),
                        ('cat:referenceID', referenceID),
                        ('cat:state', state)
                    ]) for GUID, referenceID, state in filelist
                ])
            ])
        )
        response, _, _ = self.call(tree)
        node = arc.XMLNode(response)
        return int(str(node.Child().Child().Get('nextReportTime')))

class ManagerClient(Client):
    """ Client for the Storage Manager service. """
    
    def __init__(self, url, print_xml = False):
        """ Constructior of the client.
        
        ManagerClient(url, print_xml = False)
        
        url is the URL of the Manager service
        if print_xml is true this will print the SOAP messages
        """
        # sets the namespace
        ns = arc.NS({'man':manager_uri})
        # calls the superclass' constructor
        Client.__init__(self, url, ns, print_xml)

    def stat(self, requests):
        """ Get metadata of a file or collection
        
        stat(requests)
        
        requests is a dictionary where requestIDs are the keys, and Logical Names are the values.
        this method returns a dictionary for each requestID which contains all the metadata with (section, dictionary) as the key
        
        e.g.
        
        In: {'frodo':'/', 'sam':'/testfile'}
        Out: 
            {'frodo': {('catalog', 'type'): 'collection',
                       ('entries', 'testdir'): '4cabc8cb-599d-488c-a253-165f71d4e180',
                       ('entries', 'testfile'): 'cf05727b-73f3-4318-8454-16eaf10f302c',
                       ('states', 'closed'): '0'},
             'sam': {('catalog', 'type'): 'file',
                     ('locations', 'http://localhost:60000/Element 00d6388c-42df-441c-8d9f-bd78c2c51667'): 'alive',
                     ('locations', 'http://localhost:60000/Element 51c9b49a-1472-4389-90d2-0f18b960fe29'): 'alive',
                     ('states', 'checksum'): '0927c28a393e8834aa7b838ad8a69400',
                     ('states', 'checksumType'): 'md5',
                     ('states', 'neededReplicas'): '5',
                     ('states', 'size'): '11'}}
        """
        tree = XMLTree(from_tree =
            ('man:stat', [
                ('man:statRequestList', [
                    ('man:statRequestElement', [
                        ('man:requestID', requestID),
                        ('man:LN', LN) 
                    ]) for requestID, LN in requests.items()
                ])
            ])
        )
        msg, status, reason = self.call(tree)
        xml = arc.XMLNode(msg)
        elements = parse_node(xml.Child().Child().Child(),
            ['requestID', 'metadataList'], single = True, string = False)
        return dict([(str(requestID), parse_metadata(metadataList))
            for requestID, metadataList in elements.items()])

    def getFile(self, requests):
        """ Initiate download of a file.
        
        getFile(requests)
        
        requests is a dicitonary with requestID as key and (LN, protocols) as value,
            where LN is the Logical Name
            protocols is a list of strings: the supported transfer protocols by the client
        returns a dictionary with requestID as key and [success, TURL, protocol] as value, where
            success is the status of the request
            TURL is the Transfer URL
            protocol is the name of the choosen protocol
            
        Example:
        In: {'1':['/', ['byteio']], 'a':['/testfile', ['ftp', 'byteio']]}
        Out: 
            {'1': ['is not a file', '', ''],
             'a': ['done',
                   'http://localhost:60000/byteio/29563f36-e9cb-47eb-8186-0d720adcbfca',
                   'byteio']}
        """
        tree = XMLTree(from_tree =
            ('man:getFile', [
                ('man:getFileRequestList', [
                    ('man:getFileRequestElement', [
                        ('man:requestID', rID),
                        ('man:LN', LN)
                    ] + [
                        ('man:protocol', protocol) for protocol in protocols
                    ]) for rID, (LN, protocols) in requests.items()
                ])
            ])
        )
        msg, _, _ = self.call(tree)
        xml = arc.XMLNode(msg)
        return parse_node(xml.Child().Child().Child(), ['requestID', 'success', 'TURL', 'protocol'])
    
    def putFile(self, requests):
        """ Initiate uploading a file.
        
        putFile(requests)
        
        requests is a dictionary with requestID as key, and (LN, metadata, protocols) as value, where
            LN is the Logical Name
            metadata is a dictionary with (section,property) as key, it contains the metadata of the new file
            protocols is a list of protocols supported by the client
        returns a dictionary with requestID as key, and (success, TURL, protocol) as value, where
            success is the state of the request
            TURL is the transfer URL
            protocol is the name of the choosen protocol
            
        Example:
        In: {'qwe': ['/newfile',
                    {('states', 'size') : 1055, ('states', 'checksum') : 'none', ('states', 'checksumType') : 'none'},
                    ['byteio']]}
        Out: 
            {'qwe': ['done',
                     'http://localhost:60000/byteio/d42f0993-79a8-4bba-bd86-84324367c65f',
                     'byteio']}
        """
        tree = XMLTree(from_tree =
            ('man:putFile', [
                ('man:putFileRequestList', [
                    ('man:putFileRequestElement', [
                        ('man:requestID', rID),
                        ('man:LN', LN),
                        ('man:metadataList', create_metadata(metadata, 'man')),
                    ] + [
                        ('man:protocol', protocol) for protocol in protocols
                    ]) for rID, (LN, metadata, protocols) in requests.items()
                ])
            ])
        )
        msg, _, _ = self.call(tree)
        xml = arc.XMLNode(msg)
        return parse_node(xml.Child().Child().Child(), ['requestID', 'success', 'TURL', 'protocol'])

    def addReplica(self, requests, protocols):
        """ Add a new replica to an existing file.
        
        addReplica(requests, protocols)
        
        requests is a dictionary with requestID as key and GUID as value.
        protocols is a list of protocols supported by the client
        
        returns a dictionary with requestID as key and (success, TURL, protocol) as value, where
            success is the status of the request,
            TURL is the transfer URL
            protocol is the choosen protocol
            
        Example:
        
        In: requests = {'001':'c9c82371-4773-41e4-aef3-caf7c7eaf6f8'}, protocols = ['http','byteio']
        Out:
            {'001': ['done',
                     'http://localhost:60000/byteio/c94a77a1-347c-430c-ae1b-02d83786fb2d',
                    'byteio']}
        """
        tree = XMLTree(from_tree =
            ('man:addReplica', [
                ('man:addReplicaRequestList', [
                    ('man:putReplicaRequestElement', [
                        ('man:requestID', rID),
                        ('man:GUID', GUID),
                    ]) for rID, GUID in requests.items()
                ])
            ] + [
                ('man:protocol', protocol) for protocol in protocols
            ])
        )
        msg, _, _ = self.call(tree)
        xml = arc.XMLNode(msg)
        return parse_node(xml.Child().Child().Child(), ['requestID', 'success', 'TURL', 'protocol'])

    def makeCollection(self, requests):
        """ Create a new collection.
        
        makeCollection(requests)

        requests is a dictionary with requestID as key and (LN, metadata) as value, where
            LN is the Logical Name
            metadata is the metadata of the new collection, a dictionary with (section, property) as key
        returns a dictionary with requestID as key and the state of the request as value
        
        In: {'b5':['/coloredcollection', {('metadata','color') : 'light blue'}]}
        Out: {'b5': 'done'}
        """
        tree = XMLTree(from_tree =
            ('man:makeCollection', [
                ('man:makeCollectionRequestList', [
                    ('man:makeCollectionRequestElement', [
                        ('man:requestID', rID),
                        ('man:LN', LN),
                        ('man:metadataList', create_metadata(metadata, 'man'))
                    ]) for rID, (LN, metadata) in requests.items()
                ])
            ])
        )
        msg, _, _ = self.call(tree)
        xml = arc.XMLNode(msg)
        return parse_node(xml.Child().Child().Child(), ['requestID', 'success'], single = True)

    def list(self, requests, neededMetadata = []):
        """ List the contents of a collection.
        
        list(requests, neededMetadata = [])
        
        requests is a dictionary with requestID as key and Logical Name as value
        neededMetadata is a list of (section, property) pairs
            if neededMetadata is empty, list will return all metadata for each collection-entry
            otherwise just those values will be returnd which has a listed (section, property)
            if a property is empty means that all properties will be listed from that section
        returns a dictionary with requestID as key and (entries, status) as value, where
            entries is a dictionary with the entry name as key and (GUID, metadata) as value
            status is the status of the request
        
        Example:
        In: requests = {'jim': '/', 'kirk' : '/testfile'}, neededMetadata = [('states','size'),('catalog','type')]
        Out: 
            {'jim': ({'coloredcollection': ('cab8d235-4afa-4e33-a85f-fde47b0240d1', {('catalog', 'type'): 'collection'}),
                      'newdir': ('3b200a34-0d63-4d15-9b01-3693685928bc', {('catalog', 'type'): 'collection'}),
                      'newfile': ('c9c82371-4773-41e4-aef3-caf7c7eaf6f8', {('catalog', 'type'): 'file', ('states', 'size'): '1055'}),
                      'testdir': ('4cabc8cb-599d-488c-a253-165f71d4e180', {('catalog', 'type'): 'collection'}),
                      'testfile': ('cf05727b-73f3-4318-8454-16eaf10f302c', {('catalog', 'type'): 'file', ('states', 'size'): '11'})},
                     'found'),
             'kirk': ({}, 'is a file')}
        """
        tree = XMLTree(from_tree =
            ('man:list', [
                ('man:listRequestList', [
                    ('man:listRequestElement', [
                        ('man:requestID', requestID),
                        ('man:LN', LN)
                    ]) for requestID, LN in requests.items()
                ]),
                ('man:neededMetadataList', [
                    ('man:neededMetadataElement', [
                        ('man:section', section),
                        ('man:property', property)
                    ]) for section, property in neededMetadata
                ])
            ])
        )
        msg, _, _ = self.call(tree)
        xml = arc.XMLNode(msg)
        elements = parse_node(xml.Child().Child().Child(),
            ['requestID', 'entries', 'status'], string = False)
        return dict([
            (   str(requestID), 
                (dict([(str(name), (str(GUID), parse_metadata(metadataList))) for name, (GUID, metadataList) in
                    parse_node(entries, ['name', 'GUID', 'metadataList'], string = False).items()]),
                str(status))
            ) for requestID, (entries, status) in elements.items()
        ])

    def move(self, requests):
        """ Move a file or collection within the global namespace.
        
        move(requests)
        
        requests is a dictionary with requestID as key, and (sourceLN, targetLN, preserveOriginal) as value, where
            sourceLN is the source Logical Name
            targetLN is the target Logical Name
            preserverOriginal is True if we want to create a hardlink
        returns a dictionary with requestID as key and status as value
        
        Example:
        In: {'shoo':['/testfile','/newname',False]}
        Out: {'shoo': ['moved']}
        """
        tree = XMLTree(from_tree =
            ('man:move', [
                ('man:moveRequestList', [
                    ('man:moveRequestElement', [
                        ('man:requestID', requestID),
                        ('man:sourceLN', sourceLN),
                        ('man:targetLN', targetLN),
                        ('man:preserveOriginal', preserveOriginal and true or false)
                    ]) for requestID, (sourceLN, targetLN, preserveOriginal) in requests.items()
                ])
            ])
        )
        msg, _, _ = self.call(tree)
        xml = arc.XMLNode(msg)
        return parse_node(xml.Child().Child().Child(), ['requestID', 'status'], single = False)

class ElementClient(Client):

    def __init__(self, url, print_xml = False):
        ns = arc.NS({'se':element_uri})
        Client.__init__(self, url, ns, print_xml)

    def _putget(self, putget, requests):
        tree = XMLTree(from_tree =
            ('se:' + putget, [
                ('se:' + putget + 'RequestList', [
                    ('se:' + putget + 'RequestElement', [
                        ('se:requestID', requestID),
                        ('se:' + putget + 'RequestDataList', [
                            ('se:' + putget + 'RequestDataElement', [
                                ('se:property', property),
                                ('se:value', value)
                            ]) for property, value in requestData
                        ])
                    ]) for requestID, requestData in requests.items()
                ])
            ])
        )
        msg, _, _ = self.call(tree)
        xml = arc.XMLNode(msg)
        try:
            response = dict([
                (str(node.Get('requestID')), [
                    (str(n.Get('property')), str(n.Get('value')))
                        for n in get_child_nodes(node.Get(putget + 'ResponseDataList'))
                ]) for node in get_child_nodes(xml.Child().Child().Child())])
            return response
        except:
            raise Exception, msg

    def put(self, requests):
        return self._putget('put', requests)

    def get(self, requests):
        return self._putget('get', requests)

    def stat(self, requests):
        tree = XMLTree(from_tree =
            ('se:stat', [
                ('se:statRequestList', [
                    ('se:statRequestElement', [
                        ('se:requestID', requestID),
                        ('se:referenceID', referenceID)
                    ]) for requestID, referenceID in requests.items()
                ])
            ])
        )
        msg, _, _ = self.call(tree)
        xml = arc.XMLNode(msg)
        return parse_to_dict(xml.Child().Child().Child(),
            ['requestID', 'referenceID', 'state', 'checksumType', 'checksum', 'acl', 'size', 'GUID', 'localID'])

    def toggleReport(self, doReporting):
        tree = XMLTree(from_tree =
            ('se:toggleReport', [
                ('se:doReporting', doReporting and true or false)
            ])
        )
        return self.call(tree, True)

class NotifyClient(Client):

    def __init__(self, url, print_xml = False):
        ns = arc.NS({'se':element_uri})
        Client.__init__(self, url, ns, print_xml)

    def notify(self, subject, state):
        tree = XMLTree(from_tree =
            ('se:notify', [
                ('se:subject', subject),
                ('se:state', state)
            ])
        )
        msg, _, _ = self.call(tree)
        return msg

class ByteIOClient(Client):

    def __init__(self, url):
        ns = arc.NS({'rb':rbyteio_uri})
        Client.__init__(self, url, ns)

    def read(self, start_offset = None, bytes_per_block = None, num_blocks = None, stride = None, file = None):
        request = []
        if start_offset is not None:
            request.append(('rb:start-offset', start_offset))
        if bytes_per_block is not None:
            request.append(('rb:bytes-per-block', bytes_per_block))
        if num_blocks is not None:
            request.append(('rb:num-blocks', num_blocks))
        if stride is not None:
            request.append(('rb:stride', stride))
        tree = XMLTree(from_tree = ('rb:read', request))
        msg, _, _ = self.call(tree)
        xml = arc.XMLNode(msg)
        data_encoded = str(xml.Child().Child().Get('transfer-information'))
        if file:
            file.write(base64.b64decode(data_encoded))
            file.close()
        else:
            return base64.b64decode(data_encoded)

    def write(self, data, start_offset = None, bytes_per_block = None, stride = None):
        if isinstance(data,file):
            data = data.read()
        out = arc.PayloadSOAP(self.ns)
        request_node = out.NewChild('rb:write')
        if start_offset is not None:
            request_node.NewChild('rb:start-offset').Set(str(start_offset))
        if bytes_per_block is not None:
            request_node.NewChild('rb:bytes-per-block').Set(str(bytes_per_block))
        if stride is not None:
            request_node.NewChild('rb:stride').Set(str(stride))
        transfer_node = request_node.NewChild('rb:transfer-information')
        transfer_node.NewAttribute('transfer-mechanism').Set(byteio_simple_uri)
        encoded_data = base64.b64encode(data)
        transfer_node.Set(encoded_data)
        resp, _, _ = self.call_raw(out)
        return resp

################### Gateway Client ############

class GatewayClient(Client):

	def __init__(self, url):
			
		ns = arc.NS({'gateway':gateway_uri})
	        # calls the superclass' constructor
        	Client.__init__(self, url, ns)

	def getFile(self, requests):

        	
		"""requests will come with the source and the destination URLS of the file. """		
		
		sourceURL = requests[0]
		destinationURL = requests[1]	
		tree = XMLTree(from_tree = ('gateway:getFile', [
						('gateway:URLs',[
							('gateway:sourceURL',sourceURL),
							('gateway:destinationURL',destinationURL)
								])
							]))
		print tree
		msg, _, _ = self.call(tree)
		xml = arc.XMLNode(msg)
		elements = parse_node(xml.Child().Child().Child(),
                ['ID', 'say','oo'], string = True)
                return elements

	def putFile(self, requests):


                """requests will come with the source and the destination URLS of the file. """

                sourceURL = requests[0]
                destinationURL = requests[1]
                tree = XMLTree(from_tree = ('gateway:putFile', [
                                                ('gateway:URLs',[
                                                        ('gateway:sourceURL',sourceURL),
                                                        ('gateway:destinationURL',destinationURL)
                                                                ])
                                                        ]))
                print tree
                msg, _, _ = self.call(tree)
                xml = arc.XMLNode(msg)
                elements = parse_node(xml.Child().Child().Child(),
                ['ID', 'success','status'], string = True)
                return elements

	def list(self, requests):

        	tree = XMLTree(from_tree = ('gateway:list', [('gateway:say','HI!')]))
		msg, _, _ = self.call(tree)
        	xml = arc.XMLNode(msg)
		elements = parse_node(xml.Child().Child().Child(),
                ['ID', 'success','status'], string = True)
                return elements	


class ExternalStorageInformationClient(Client):

	def __init__(self, url):
		
		ns = arc.NS({'externalStorageInformation':externalInfo_uri})
                # calls the superclass' constructor
                Client.__init__(self, url, ns)

	def getInfo(self):

		print "External Storage getInfo function"
		tree = XMLTree(from_tree = ('externalStorageInformation:getInfo', [('externalStorageInformation:say','Hi!')]))
		msg, _, _  = self.call(tree)
		xml = arc.XMLNode(msg)
		elements = parse_node(xml.Child().Child().Child(),
                ['ID', 'say', 'oo'], string = True)
		
                return elements		
class TransferClient(Client):

        def __init__(self, url):

                ns = arc.NS({'transfer':transfer_uri})
                # calls the superclass' constructor
                Client.__init__(self, url, ns)

        def transferData(self):

                print "Transfer test function"
		tree = XMLTree(from_tree = ('transfer:transferData', [('transfer:say','Hi!')]))
                msg, _, _ = self.call(tree)
                xml = arc.XMLNode(msg)

####################################################
