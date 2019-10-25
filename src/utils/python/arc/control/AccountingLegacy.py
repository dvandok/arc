from __future__ import print_function
from __future__ import absolute_import

from .ControlCommon import *
from .JuraArchive import JuraArchive
import sys
import os
import subprocess
import tempfile


def add_timeframe_args(parser, required=False):
    parser.add_argument('-b', '--start-from', type=valid_datetime_type, required=required,
                        help='Limit the start time of the records (YYYY-MM-DD [HH:mm[:ss]])')
    parser.add_argument('-e', '--end-till', type=valid_datetime_type, required=required,
                        help='Limit the end time of the records (YYYY-MM-DD [HH:mm[:ss]])')


def complete_owner_vo(prefix, parsed_args, **kwargs):
    arcconf = get_parsed_arcconf(parsed_args.config)
    return LegacyAccountingControl(arcconf).complete_vos(prefix, parsed_args)


def complete_owner(prefix, parsed_args, **kwargs):
    arcconf = get_parsed_arcconf(parsed_args.config)
    return LegacyAccountingControl(arcconf).complete_users(prefix, parsed_args)


class LegacyAccountingControl(ComponentControl):
    def __init__(self, arcconfig):
        self.logger = logging.getLogger('ARCCTL.Accounting')
        # arc config
        if arcconfig is None:
            self.logger.error('Failed to parse arc.conf. Jura configuration is unavailable.')
            sys.exit(1)
        self.arcconfig = arcconfig
        self.runconfig = None
        # archive
        archive_dir = arcconfig.get_value('archivedir', 'arex/jura/archiving')
        if archive_dir is None:
            self.logger.warning('Accounting records archiving is not enabled! '
                                'It is not possible to operate with accounting information or doing re-publishing.')
            sys.exit(1)
        # archive manager
        accounting_db_dir = arcconfig.get_value('dbdir', 'arex/jura/archiving')
        if accounting_db_dir is None:
            accounting_db_dir = archive_dir
        self.archive = JuraArchive(archive_dir, accounting_db_dir)
        # logs
        self.logfile = arcconfig.get_value('logfile', 'arex/jura')
        if self.logfile is None:
            self.logfile = '/var/log/arc/jura.log'
        self.ssmlog = '/var/spool/arc/ssm/ssmsend.log'  # hardcoded in JURA_DEFAULT_DIR_PREFIX and ssm/sender.cfg

    def __del__(self):
        if self.runconfig is not None:
            os.unlink(self.runconfig)

    def __jura_bin(self):
        """Return legacy jura binary invocation command"""
        # dump runconfig
        _, self.runconfig = tempfile.mkstemp(suffix='.conf', prefix='arcctl.jura.')
        self.logger.debug('Dumping runtime configuration for Jura to %s', self.runconfig)
        self.arcconfig.save_run_config(self.runconfig)
        # setup environment (x509)
        x509_cert_dir = self.arcconfig.get_value('x509_cert_dir', 'common')
        x509_host_cert = self.arcconfig.get_value('x509_host_cert', 'common')
        x509_host_key = self.arcconfig.get_value('x509_host_key', 'common')
        if x509_cert_dir is not None:
            os.environ['X509_CERT_DIR'] = x509_cert_dir
        if x509_host_cert is not None:
            os.environ['X509_USER_CERT'] = x509_host_cert
        if x509_host_key is not None:
            os.environ['X509_USER_KEY'] = x509_host_key
        # debug level
        loglevel = logging.getLogger('ARC').getEffectiveLevel()
        loglevel = {50: 'FATAL', 40: 'ERROR', 30: 'WARNING', 20: 'INFO', 10: 'DEBUG'}[loglevel]
        # return command
        return ARC_LIBEXEC_DIR + '/jura -c {0} -d {1} '.format(self.runconfig, loglevel)

    def __ensure_accounting_db(self, args):
        """Ensure accounting database availabiliy"""
        if self.archive.db_exists():
            self.archive.db_connection_init()
        elif args.db_init:
            self.logger.info('Migrating legacy Jura archive to database')
            self.archive.process_records()
        else:
            self.logger.error('Jura archive database is not initialized. '
                              'Most probably jura-archive-manager is not active. '
                              'If you want to force database init from arcctl add --db-init option.')
            sys.exit(1)

    @staticmethod
    def __construct_filter(args):
        filters = {}
        if args.start_from:
            filters['startfrom'] = args.start_from
        if args.end_till:
            filters['endtill'] = args.end_till
        if hasattr(args, 'filter_vo') and args.filter_vo:
            filters['vos'] = args.filter_vo
        if hasattr(args, 'filter_user') and args.filter_user:
            filters['owners'] = args.filter_user
        return filters

    def stats(self, args):
        # construct filter
        filters = self.__construct_filter(args)
        # loop over types
        for t in args.type:
            self.logger.info('Showing the %s archived records statistics', t.upper())
            filters['type'] = t
            # show stats data (particular info requested)
            if args.jobs:
                print(self.archive.get_records_count(filters))
            elif args.walltime:
                print(self.archive.get_records_walltime(filters))
            elif args.cputime:
                print(self.archive.get_records_cputime(filters))
            elif args.vos:
                print('\n'.join(self.archive.get_records_vos(filters)))
            elif args.users:
                print('\n'.join(self.archive.get_records_owners(filters)))
            else:
                # show summary info
                count = self.archive.get_records_count(filters)
                if count:
                    sfrom, etill = self.archive.get_records_dates(filters)
                    walltime = self.archive.get_records_walltime(filters)
                    cputime = self.archive.get_records_cputime(filters)
                    print('Statistics for {0} jobs from {1} till {2}:\n'
                          '  Number of jobs: {3:>16}\n'
                          '  Total WallTime: {4:>16}\n'
                          '  Total CPUTime:  {5:>16}'.format(t.upper(), sfrom, etill, count, walltime, cputime))
                else:
                    print('There are no {0} archived records available'.format(t.upper()))

    def republish(self, args):
        if args.start_from > args.end_till:
            self.logger.error('Records start time should be before the end time.')
            sys.exit(1)
        # export necessary records to republishing directory
        filters = self.__construct_filter(args)
        filters['type'] = 'apel' if args.apel_url else 'sgas'
        exportdir = self.archive.export_records(filters=filters)
        # define timeframe for Jura
        jura_startfrom = args.start_from.strftime('%Y.%m.%d').replace('.0', '.')
        jura_endtill = args.end_till.strftime('%Y.%m.%d').replace('.0', '.')
        jura_bin = self.__jura_bin()
        command = ''
        if args.apel_url:
            command = '{0} -u {1} -t {2} -r {3}-{4} -A {5}'.format(
                jura_bin,
                args.apel_url,
                args.apel_topic,
                jura_startfrom, jura_endtill,
                exportdir
            )
        elif args.sgas_url:
            command = '{0} -u {1} -r {2}-{3} -A {4}'.format(
                jura_bin,
                args.sgas_url,
                jura_startfrom, jura_endtill,
                exportdir
            )
        self.logger.info('Running the following command to republish accounting records: %s', command)
        subprocess.call(command.split(' '))
        # clean export dir
        self.archive.export_remove()

    def logs(self, ssm=False):
        logfile = self.logfile
        if ssm:
            logfile = self.ssmlog
        pager_bin = 'less'
        if 'PAGER' in os.environ:
            pager_bin = os.environ['PAGER']
        p = subprocess.Popen([pager_bin, logfile])
        p.communicate()

    def control(self, args):
        if args.legacyaction == 'stats':
            self.__ensure_accounting_db(args)
            self.stats(args)
        elif args.legacyaction == 'logs':
            self.logs(args.ssm)
        elif args.legacyaction == 'republish':
            self.__ensure_accounting_db(args)
            self.republish(args)
        else:
            self.logger.critical('Unsupported legacy accounting action %s', args.legacyaction)
            sys.exit(1)

    def complete_vos(self, prefix, args):
        self.__ensure_accounting_db(args)
        return self.archive.get_all_vos(prefix)

    def complete_users(self, prefix, args):
        self.__ensure_accounting_db(args)
        return self.archive.get_all_owners(prefix)

    @staticmethod
    def register_parser(root_parser):
        accounting_ctl = root_parser.add_parser('legacy', help='Legacy jura accounting records management')
        accounting_ctl.set_defaults(handler_class=LegacyAccountingControl)

        accounting_actions = accounting_ctl.add_subparsers(title='Legacy Accounting Actions', dest='legacyaction',
                                                           metavar='ACTION', help='DESCRIPTION')

        # republish
        accounting_republish = accounting_actions.add_parser('republish',
                                                             help='Republish usage records from legacy jura archive')
        add_timeframe_args(accounting_republish, required=True)
        accounting_url = accounting_republish.add_mutually_exclusive_group(required=True)
        accounting_url.add_argument('-a', '--apel-url',
                                    help='Specify APEL server URL (e.g. https://mq.cro-ngi.hr:6163)')
        accounting_url.add_argument('-s', '--sgas-url',
                                    help='Specify SGAS server URL (e.g. https://grid.uio.no:8001/logger)')
        accounting_republish.add_argument('--db-init', action='store_true',
                                      help='Force accounting database init from arcctl')
        accounting_republish.add_argument('-t', '--apel-topic', default='/queue/global.accounting.cpu.central',
                                          choices=['/queue/global.accounting.cpu.central',
                                                   '/queue/global.accounting.test.cpu.central'],
                                          help='Redefine APEL topic (default is %(default)s)')

        # logs
        accounting_logs = accounting_actions.add_parser('logs', help='Show accounting logs')
        accounting_logs.add_argument('-s', '--ssm', help='Show SSM logs instead of Jura logs', action='store_true')

        # stats
        accounting_stats = accounting_actions.add_parser('stats', help='Show archived records statistics')
        accounting_stats.add_argument('-t', '--type', help='Accounting system type',
                                     choices=['apel', 'sgas'], action='append', required=True)
        add_timeframe_args(accounting_stats)
        accounting_stats.add_argument('--db-init', action='store_true',
                                      help='Force accounting database init from arcctl')
        accounting_stats.add_argument('--filter-vo', help='Count only the jobs owned by this VO(s)',
                                     action='append').completer = complete_owner_vo
        accounting_stats.add_argument('--filter-user', help='Count only the jobs owned by this user(s)',
                                     action='append').completer = complete_owner

        accounting_stats_info = accounting_stats.add_mutually_exclusive_group(required=False)
        accounting_stats_info.add_argument('-j', '--jobs', help='Show number of jobs', action='store_true')
        accounting_stats_info.add_argument('-w', '--walltime', help='Show total WallTime', action='store_true')
        accounting_stats_info.add_argument('-c', '--cputime', help='Show total CPUTime', action='store_true')
        accounting_stats_info.add_argument('-v', '--vos', help='Show VOs that owns jobs', action='store_true')
        accounting_stats_info.add_argument('-u', '--users', help='Show users that owns jobs', action='store_true')
