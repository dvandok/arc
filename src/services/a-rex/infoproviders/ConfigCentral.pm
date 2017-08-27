package ConfigCentral;

# Builds an intermediate config hash that is used by the A-REX infoprovider and LRMS control scripts
# Can read XML and INI

## RESTRUCTURING PHASE ################
## changes are identified by the tags
## #C changenumber
#######################################

use strict;
use warnings;
use File::Basename;

# added to parse JSON stuff
binmode STDOUT, ":utf8";
use utf8;

use XML::Simple;
use Data::Dumper qw(Dumper);
use JSON::XS;
#use Data::Dumper::Concise;

use IniParser;
use InfoChecker;
use LogUtils;

# while parsing, loglevel is WARNING (the default)
our $log = LogUtils->getLogger(__PACKAGE__);

#######################################################################
## Representation of configuration data after parsing using the python
## parser. Mostly 1:1 with arc.conf, but there are exceptions indicated
## below.
#######################################################################

#my $pylrms_options = { # former lrms_options, added missing data
	#lrms => '',
    #pbs_bin_path => '*',
    #pbs_log_path => '*',
    #dedicated_node_string => '*',
    #maui_bin_path => '*',
    #condor_bin_path => '*',
    #condor_config => '*',
    #condor_rank => '*',
    #sge_bin_path => '*',
    #sge_root => '*',
    #sge_cell => '*',
    #sge_qmaster_port => '*',
    #sge_execd_port => '*',
    #lsf_bin_path => '*',
    #lsf_profile_path => '*',
    #lsf_architecture => '*',
    #ll_bin_path => '*',
    #ll_consumable_resources => '*',
    #slurm_bin_path => '*',
    #slurm_wakeupperiod => '*',
    #dgbridge_stage_dir => '*',
    #dgbridge_stage_prepend => '*',
    #boinc_db_host => '*',
    #boinc_db_port => '*',
    #boinc_db_name => '*',
    #boinc_db_user => '*',
    #boinc_db_pass => '*',
#}; 

#my $pylrms_queue_options = { # former lrms_share_options
    #pbs_queue_node => '*', # previously queue_node_string
    #condor_requirements => '*',
    #sge_jobopts => '*',
    #lsf_architecture => '*',
    #ll_consumable_resources => '*',
#};
#my $pyxenv_options = {
    #Platform => '*',
    #Homogeneous => '*',
    #PhysicalCPUs => '*',
    #LogicalCPUs => '*',
    #CPUVendor => '*',
    #CPUModel => '*',
    #CPUVersion => '*',
    #CPUClockSpeed => '*',
    #CPUTimeScalingFactor => '*',
    #WallTimeScalingFactor => '*',
    #MainMemorySize => '*',
    #VirtualMemorySize => '*',
    #OSFamily => '*',
    #OSName => '*',
    #OSVersion => '*',
    #VirtualMachine => '*',
    #NetworkInfo => '*',
    #ConnectivityIn => '*',
    #ConnectivityOut => '*',
    #Benchmark => [ '*' ],
    #OpSys => [ '*' ],
    #nodecpu => '*',
#};

## [queue/NAME] blocks
#my $pyqueue_options = { # former part of share_options
    #totalcpus => '*',
    #defaultmemory => '*',
    #authorizedvo =>  [ '*' ],
#};

## commodity datastructure to represent glue2 stuff
## should be different from what is done now, 
## shares should be created per policy and
## use the queue info
#my $pyqueue_glue2_options = { # former share_options, should only old GLUE2 concepts
    #MaxVirtualMemory => '*',
    #MaxSlotsPerJob => '*',
    #SchedulingPolicy => '*',
    #Preemption => '*',
#};

## [gridmanager] block
#my $pygridmanager_options = { # former gmuser_options
    #controldir => '',
    #sessiondir => [ '' ],
    #cachedir => [ '*' ],
    #cachesize => '*',
    #defaultttl => '*',
    #infoproviders_timelimit => '*', # former infoproviders_timeout in infosys section
#};

## [common] block
#my $pycommon_options = { # former gmcommon_options, cleared old arc.conf data
    #gmconfig => '*', # TODO: what was this meant for? Maybe A-REX XML?
    #wsurl => '*',
    #hostname => '*',
    #maxjobs => '*',
    #maxload => '*',
    #maxloadshare => '*',
    #wakeupperiod => '*',
    #gridmap => '*',
    #x509_host_key => '*',
    #x509_host_cert => '*',
    #x509_cert_dir => '*',
    #runtimedir => '*',
    #gnu_time => '*',
    #shared_filesystem => '*',
    #shared_scratch => '*',
    #scratchdir => '*',
    #enable_perflog_reporting => '*',
    #perflogdir => '*'
#};

## TODO: my something for interfaces
##    enable_emies_interface => '*',
##    enable_arc_interface => '*',

## [lrms/ssh] block
#my $pylrmsssh_options = { # former sshcommon_options
    #remote_user => '*',
    #remote_host => '*',
    #remote_sessiondir => '*',
    #private_key => '*',
#};

## [infosys] blocks

#my $pyinfosys_options = {
     #validity_ttl => '*'	
#}

## [infosys/ldap] block
#my $pyinfosys_ldap_options = { # former ldap_infosys_options
    #port => '*' # former SlapdPort
#};

## TODO: [infosys/glue2] and [infosys/glue2/ldap]
##    infosys_glue2_ldap_showactivities => '*',
    ##infosys_glue2_service_qualitylevel => '*',
## 
## TODO: change to new LDAP format 
## my $admindomain_options = {
##    Name => '*',
##    OtherInfo => [ '*' ],
##    Description => '*',
##    WWW => '*',
##    Distributed => '*',
##    Owner => '*'
##};

## TODO: interfaces blocks
##    infosys_nordugrid => '*',
##    infosys_glue12 => '*',
##    infosys_glue2_ldap => '*',

## TODO: [infosys/ldap/bdii] block
##    infosys_ldap_run_dir => '*',
##    bdii_var_dir => '*',
##    bdii_tmp_dir => '*',
##    bdii_run_dir => '*',
##    bdii_update_pid_file => '*',

#my $pygridftpd_options = { # former gridftpd_options
##    GridftpdEnabled => '*', # may not be needed anymore as we just check the block
    #port => '*', # former GridftpdPort
    #path => '/jobs', # former GridftpdMountPoint, now hardcoded
    #allownew => '*', # former GridftpdAllowNew
    #pidfile => '*', # former GridftpdPidFile
#};

## # # # # # # # # # # # # #

#my $config_schema = {
    #defaultLocalName => '*',
    #debugLevel => '*',
    #ProviderLog => '*',
    #PublishNordugrid => '*',
    #AdminDomain => '*',
    #ttl => '*',
    #admindomain => { %$admindomain_options },
    #%$gmcommon_options,
    #%$sshcommon_options,
    #%$gridftpd_options,
    #%$ldap_infosys_options,
    #%$lrms_options,
    #%$lrms_share_options,
    #control => {
        #'*' => {
            #%$gmuser_options
        #}
    #},
    #service => {
        #OtherInfo => [ '*' ],
        #StatusInfo => [ '*' ],
        #Downtime => '*',
        #ClusterName => '*',
        #ClusterAlias => '*',
        #ClusterComment => '*',
        #ClusterOwner => [ '*' ],
        #Middleware => [ '*' ],
        #AuthorizedVO => [ '*' ],
        #LocalSE => [ '*' ],
        #InteractiveContactstring => [ '*' ],
        #%$xenv_options,
        #%$share_options,
    #},
    #location => {
        #Name => '*',
        #Address => '*',
        #Place => '*',
        #Country => '*',
        #PostCode => '*',
        #Latitude => '*',
        #Longitude => '*',
    #},
    #contacts => [ {
        #Name => '*',
        #OtherInfo => [ '*' ],
        #Detail => '',
        #Type => '',
    #} ],
    #accesspolicies => [ {
        #Rule => [ '' ],
        #UserDomainID => [ '' ],
    #} ],
    #mappingpolicies => [ {
        #ShareName => [ '' ],
        #Rule => [ '' ],
        #UserDomainID => [ '' ],
    #} ],
    #xenvs => {
        #'*' => {
            #OtherInfo => [ '*' ],
            #NodeSelection => {
                #Regex => [ '*' ],
                #Command => [ '*' ],
                #Tag => [ '*' ],
            #},
            #%$xenv_options,
        #}
    #},
    #shares => {
        #'*' => {
            #Description => '*',
            #OtherInfo => [ '*' ],
            #MappingQueue => '*',
            #ExecutionEnvironmentName => [ '' ],
            #%$share_options,
            #%$lrms_share_options,
        #}
    #}
    #infosys => {
	  # loglevel => ''
    #}
#};

#my $allbools = [ qw(
                 #PublishNordugrid Homogeneous VirtualMachine
                 #ConnectivityIn ConnectivityOut Preemption
                 #infosys_nordugrid infosys_glue12 infosys_glue2_ldap infosys_glue2_ldap_showactivities
                 #GridftpdEnabled GridftpdAllowNew Distributed enable_arc_interface enable_emies_interface enable_perflog_reporting) ];

######################################################################
# Legacy Internal representation of configuration data after parsing #
######################################################################

my $lrms_options = {
    pbs_bin_path => '*',
    pbs_log_path => '*',
    dedicated_node_string => '*',
    maui_bin_path => '*',
    condor_bin_path => '*',
    condor_config => '*',
    condor_rank => '*',
    sge_bin_path => '*',
    sge_root => '*',
    sge_cell => '*',
    sge_qmaster_port => '*',
    sge_execd_port => '*',
    lsf_bin_path => '*',
    lsf_profile_path => '*',
    ll_bin_path => '*',
    slurm_bin_path => '*',
    slurm_wakeupperiod => '*',
    dgbridge_stage_dir => '*',
    dgbridge_stage_prepend => '*',
    boinc_db_host => '*',
    boinc_db_port => '*',
    boinc_db_name => '*',
    boinc_db_user => '*',
    boinc_db_pass => '*',
};
my $lrms_share_options = {
    queue_node_string => '*',
    condor_requirements => '*',
    sge_jobopts => '*',
    lsf_architecture => '*',
    ll_consumable_resources => '*',
};
my $xenv_options = {
    Platform => '*',
    Homogeneous => '*',
    PhysicalCPUs => '*',
    LogicalCPUs => '*',
    CPUVendor => '*',
    CPUModel => '*',
    CPUVersion => '*',
    CPUClockSpeed => '*',
    CPUTimeScalingFactor => '*',
    WallTimeScalingFactor => '*',
    MainMemorySize => '*',
    VirtualMemorySize => '*',
    OSFamily => '*',
    OSName => '*',
    OSVersion => '*',
    VirtualMachine => '*',
    NetworkInfo => '*',
    ConnectivityIn => '*',
    ConnectivityOut => '*',
    Benchmark => [ '*' ],
    OpSys => [ '*' ],
    nodecpu => '*',
};
my $share_options = {
    MaxVirtualMemory => '*',
    MaxSlotsPerJob => '*',
    SchedulingPolicy => '*',
    Preemption => '*',
    totalcpus => '*',
    defaultmemory => '*',
    authorizedvo =>  [ '*' ],
};
my $gmuser_options = {
    controldir => '',
    sessiondir => [ '' ],
    cachedir => [ '*' ],
    cachesize => '*',
    remotecachedir => [ '*' ],
    defaultttl => '*',
};
my $gmcommon_options = {
    lrms => '',
    gmconfig => '*',
    endpoint => '*',
    hostname => '*',
    maxjobs => '*',
    maxload => '*',
    maxloadshare => '*',
    wakeupperiod => '*',
    gridmap => '*',
    #x509_user_key => '*',  # C 5
    x509_host_key => '*',   # C 5
    #x509_user_cert => '*', # C 6
    x509_host_cert => '*',  # C 6
    x509_cert_dir => '*',
    runtimedir => '*',
    gnu_time => '*',
    shared_filesystem => '*',
    shared_scratch => '*',
    scratchdir => '*',
    enable_emies_interface => '*',
    enable_arc_interface => '*',
    enable_perflog_reporting => '*',
    perflogdir => '*'
};
my $sshcommon_options = {
    remote_user => '*',
    remote_host => '*',
    remote_sessiondir => '*',
    private_key => '*',
};
my $ldap_infosys_options = {
    SlapdPort => '*',
    infosys_ldap_run_dir => '*',
    bdii_var_dir => '*',
    bdii_tmp_dir => '*',
    bdii_run_dir => '*',
    infosys_nordugrid => '*',
    infosys_glue12 => '*',
    infosys_glue2_ldap => '*',
    bdii_update_pid_file => '*',
    infosys_glue2_ldap_showactivities => '*',
    infosys_glue2_service_qualitylevel => '*',
    infoproviders_timeout => '*',
    validity_ttl => '*'
};
my $gridftpd_options = {
    GridftpdEnabled => '*',
    GridftpdPort => '*',
    GridftpdMountPoint => '*',
    GridftpdAllowNew => '*',
    remotegmdirs => [ '*' ],
    GridftpdPidFile => '*',
};

my $admindomain_options = {
    Name => '*',
    OtherInfo => [ '*' ],
    Description => '*',
    WWW => '*',
    Distributed => '*',
    Owner => '*'
};

# # # # # # # # # # # # # #

my $config_schema = {
    defaultLocalName => '*',
    
    #ProviderLog => '*', #C 133
    PublishNordugrid => '*',
    AdminDomain => '*',
    ttl => '*',
    admindomain => { %$admindomain_options },   
    
    %$gmcommon_options,
    %$sshcommon_options,
    %$gridftpd_options,
    %$ldap_infosys_options,
    %$lrms_options,
    %$lrms_share_options,    
    control => {
        '*' => {
            %$gmuser_options
        }
    },
    service => {
        OtherInfo => [ '*' ],
        StatusInfo => [ '*' ],
        Downtime => '*',
        ClusterName => '*',
        ClusterAlias => '*',
        ClusterComment => '*',
        ClusterOwner => [ '*' ],
        Middleware => [ '*' ],
        AuthorizedVO => [ '*' ],
        LocalSE => [ '*' ],
        InteractiveContactstring => [ '*' ],
        %$xenv_options,
        %$share_options,
    },
    location => {
        Name => '*',
        Address => '*',
        Place => '*',
        Country => '*',
        PostCode => '*',
        Latitude => '*',
        Longitude => '*',
    },
    contacts => [ {
        Name => '*',
        OtherInfo => [ '*' ],
        Detail => '',
        Type => '',
    } ],
    accesspolicies => [ {
        Rule => [ '' ],
        UserDomainID => [ '' ],
    } ],
    mappingpolicies => [ {
        ShareName => [ '' ],
        Rule => [ '' ],
        UserDomainID => [ '' ],
    } ],
    xenvs => {
        '*' => {
            OtherInfo => [ '*' ],
            NodeSelection => {
                Regex => [ '*' ],
                Command => [ '*' ],
                Tag => [ '*' ],
            },
            %$xenv_options,
        }
    },
    shares => {
        '*' => {
            Description => '*',
            OtherInfo => [ '*' ],
            MappingQueue => '*',
            ExecutionEnvironmentName => [ '' ],
            %$share_options,
            %$lrms_share_options,
        }
    },
    # start of newly added items for arcconf restructuring
    infosys => {
		logfile => '*',  #C 133
		loglevel => '*', #C 134, replaces ProviderLog
		validity_ttl => '*',
		user => '*'
    },
    arex => {
		loglevel => '*', # replaces $config_schema->{debugLevel}, C 37
		infoproviders_timelimit => '*'
	}
};

my $allbools = [ qw(
                 PublishNordugrid Homogeneous VirtualMachine
                 ConnectivityIn ConnectivityOut Preemption
                 infosys_nordugrid infosys_glue12 infosys_glue2_ldap infosys_glue2_ldap_showactivities
                 GridftpdEnabled GridftpdAllowNew Distributed enable_arc_interface enable_emies_interface enable_perflog_reporting) ];

############################ Generic functions ###########################

# walks a tree of hashes and arrays while applying a function to each hash.
sub hash_tree_apply {
    my ($ref, $func) = @_;
    if (not ref($ref)) {
        return;
    } elsif (ref($ref) eq 'ARRAY') {
        map {hash_tree_apply($_,$func)} @$ref;
        return;
    } elsif (ref($ref) eq 'HASH') {
        &$func($ref);
        map {hash_tree_apply($_,$func)} values %$ref;
        return;
    } else {
        return;
    }
}

# Strips namespace prefixes from the keys of the hash passed by reference
sub hash_strip_prefixes {
    my ($h) = @_;
    my %t;
    while (my ($k,$v) = each %$h) {
        next if $k =~ m/^xmlns/;
        $k =~ s/^\w+://;
        $t{$k} = $v;
    }
    %$h=%t;
    return;
}

# Verifies that a key is an HASH reference and returns that reference
sub hash_get_hashref {
    my ($h, $key) = @_;
    my $r = ($h->{$key} ||= {});
    $log->fatal("badly formed '$key' element in XML config") unless ref $r eq 'HASH';
    return $r;
}

# Verifies that a key is an ARRAY reference and returns that reference
sub hash_get_arrayref {
    my ($h, $key) = @_;
    my $r = ($h->{$key} ||= []);
    $log->fatal("badly formed '$key' element in XML config") unless ref $r eq 'ARRAY';
    return $r;
}

# Set selected keys to either 'true' or 'false'
sub fixbools {
    my ($h,$bools) = @_;
    for my $key (@$bools) {
        next unless exists $h->{$key};
        my $val = $h->{$key};
        if ($val eq '0' or lc $val eq 'false' or lc $val eq 'no' or lc $val eq 'disable') {
            $h->{$key} = '0';
        } elsif ($val eq '1' or lc $val eq 'true' or lc $val eq 'yes' or lc $val eq 'enable' or lc $val eq 'expert-debug-on') {
            $h->{$key} = '1';
        } else {
            $log->error("Invalid value for $key");
        }
    }
    return $h;
}

sub move_keys {
    my ($h, $k, $names) = @_;
    for my $key (@$names) {
        next unless exists $h->{$key};
        $k->{$key} = $h->{$key};
        delete $h->{$key};
    }
}

sub rename_keys {
    my ($h, $k, $names) = @_;
    for my $key (keys %$names) {
        next unless exists $h->{$key};
        my $newkey = $names->{$key};
        $k->{$newkey} = $h->{$key};
        delete $h->{$key};
    }
}

##################### Read config via arcconfig-parser ################

# execute parser and get json data
sub read_json_config {
   	my ($arcconf) = @_;	
	
    # get the calling script basepath. Will be used to
    # find external scripts like arcconfig-parser.
    my $libexecpath = dirname($0);	
	
	my $jsonconfig='';
	{ 
      local $/; # slurp mode
	  open (my $jsonout, "$libexecpath/arcconfig-parser -e json -c $arcconf |") || $log->error("Python config parser error: $! at line: ".__LINE__." libexecpath: $libexecpath");
	  $jsonconfig = <$jsonout>;
	  close $jsonout;
	}
	my $config = decode_json($jsonconfig);
	#print Dumper($config);
    
    return $config;
}

sub _substitute {
    my ($config, $arc_location) = @_;
    my $control = $config->{control};

    my ($lrms, $defqueue) = split " ", $config->{lrms} || '';

    die 'Gridmap user list feature is not supported anymore. Please use @filename to specify user list.'
        if exists $control->{'*'};

    # expand user sections whose user name is like @filename
    my @users = keys %$control;
    for my $user (@users) {
        next unless $user =~ m/^\@(.*)$/;
        my $path = $1;
        my $fh;
        # read in user names from file
        if (open ($fh, "< $path")) {
            while (my $line = <$fh>) {
                chomp (my $newuser = $line);
                next if exists $control->{$newuser};         # Duplicate user!!!!
                $control->{$newuser} = { %{$control->{$user}} }; # shallow copy
            }
            close $fh;
            delete $control->{$user};
        } else {
            die "Failed opening file to read user list from: $path: $!";
        }
    }

    # substitute per-user options
    @users = keys %$control;
    for my $user (@users) {
        my @pw;
        my $home;
        if ($user ne '.') {
            @pw = getpwnam($user);
            $log->warning("getpwnam failed for user: $user: $!") unless @pw;
            $home = $pw[7] if @pw;
        } else {
            $home = "/tmp";
        }

        my $opts = $control->{$user};

        # Default for controldir, sessiondir
        if ($opts->{controldir} eq '*') {
            $opts->{controldir} = $pw[7]."/.jobstatus" if @pw;
        }
        $opts->{sessiondir} ||= ['*'];
        $opts->{sessiondir} = [ map { $_ ne '*' ? $_ : "$home/.jobs" } @{$opts->{sessiondir}} ];

        my $controldir = $opts->{controldir};
        my @sessiondirs = @{$opts->{sessiondir}};

        my $subst_opt = sub {
            my ($val) = @_;

            #  %R - session root
            $val =~ s/%R/$sessiondirs[0]/g if $val =~ m/%R/;
            #  %C - control dir
            $val =~ s/%C/$controldir/g if $val =~ m/%C/;
            if (@pw) {
                #  %U - username
                $val =~ s/%U/$user/g       if $val =~ m/%U/;
                #  %u - userid
                #  %g - groupid
                #  %H - home dir
                $val =~ s/%u/$pw[2]/g      if $val =~ m/%u/;
                $val =~ s/%g/$pw[3]/g      if $val =~ m/%g/;
                $val =~ s/%H/$home/g       if $val =~ m/%H/;
            }
            #  %L - default lrms
            #  %Q - default queue
            $val =~ s/%L/$lrms/g           if $val =~ m/%L/;
            $val =~ s/%Q/$defqueue/g       if $val =~ m/%Q/;
            #  %W - installation path
            $val =~ s/%W/$arc_location/g   if $val =~ m/%W/;
            #  %G - globus path
            my $G = $ENV{GLOBUS_LOCATION} || '/usr';
            $val =~ s/%G/$G/g              if $val =~ m/%G/;

            return $val;
        };
        if ($opts->{controldir}) {
            $opts->{controldir} = &$subst_opt($opts->{controldir});
        }
        if ($opts->{sessiondir}) {
            $opts->{sessiondir} = [ map {&$subst_opt($_)} @{$opts->{sessiondir}} ];
        }
        if ($opts->{cachedir}) {
            $opts->{cachedir} = [ map {&$subst_opt($_)} @{$opts->{cachedir}} ];
        }
        if ($opts->{remotecachedir}) {
            $opts->{remotecachedir} = [ map {&$subst_opt($_)} @{$opts->{remotecachedir}} ];
        }
    }

    # authplugin, localcred, helper: not substituted

    return $config;
}

#
# Reads the json config file passed as the first argument and produces a config
# hash conforming to $config_schema. 
#
sub build_config_from_json {
    my ($file) = @_;
    
    my $jsonconf = read_json_config($file);

    ## TODO: set and check defaults.
    ## maybe add the default checker in InfoChecker.pm?
    ## this is required for the mandatory values. Program should exit with 
    ## errors or solve the issue automatically if the mandatory value
    ## is not set.
    ## $jsonconf = InfoChecker::setDefaults($jsonconf);

    # Those values that are the same as in arc.conf will 
    # be copied and checked.
    my $config ||= {};
    $config->{service} ||= {};
    $config->{control} ||= {};
    $config->{location} ||= {};
    $config->{contacts} ||= [];
    $config->{accesspolicies} ||= [];   
    $config->{mappingpolicies} ||= [];
    $config->{xenvs} ||= {};
    $config->{shares} ||= {};
    $config->{admindomain} ||= {};

    # start of restructured pieces of information
    $config->{infosys} ||= {};
    $config->{arex} ||= {};
    #  end of restructured pieces of information


    my $common = $jsonconf->{'common'};    
    move_keys $common, $config, [keys %$gmcommon_options];
    move_keys $common, $config, [keys %$lrms_options, keys %$lrms_share_options];
    
    # C 173
    my $arex = $jsonconf->{'a-rex'};
    move_keys $arex, $config, [keys %$gmcommon_options];
    rename_keys $arex, $config, {arex_mount_point => 'endpoint'};
    
    my $ssh = $jsonconf->{'ssh'};
    move_keys $ssh, $config, [keys %$sshcommon_options];
    
    
    # C 37 changed there is no more common debuglevel
    # TODO: remove debugLevel occurences everywhere else 
    # $config->{debugLevel} = $common->{debug} if $common->{debug}; 
    

    # TODO: create a proper subconfig for this, remove references to this one
    move_keys $common, $config, [keys %$ldap_infosys_options];

    #C 133 134
    my $infosys = $jsonconf->{'infosys'};
        
    move_keys $infosys, $config->{'infosys'}, [keys %$infosys];
        
    rename_keys $infosys, $config, {port => 'SlapdPort'};
    move_keys $infosys, $config, [keys %$ldap_infosys_options];
    
    # only one grid manager user, formerly represented by a dot
    $config->{control}{'.'} ||= {};
    move_keys $arex, $config->{control}{'.'}, [keys %$gmuser_options];
    # C 173
    move_keys $arex, $config->{arex}, [keys %$arex];

    # Cherry-pick some gridftp options
    # TODO: clean this into a proper section, not the mess that is now!
    if (defined $jsonconf->{'gridftpd/jobs'}) {
        my $gconf = $jsonconf->{'gridftpd'};
        my $gjconf = $jsonconf->{'gridftpd/jobs'};
        $config->{GridftpdEnabled} = 'yes';
        $config->{GridftpdPort} = $gconf->{port} if $gconf->{port};
        $config->{GridftpdMountPoint} = $gjconf->{path} if $gjconf->{path};
        $config->{GridftpdAllowNew} = $gjconf->{allownew} if defined $gjconf->{allownew};
        # TODO: check if remote dirs have been removed
        #$config->{remotegmdirs} = $gjconf{remotegmdirs} if defined $gjconf{remotegmdirs};
        $config->{GridftpdPidFile} = $gconf->{pidfile} if defined $gconf->{pidfile};
    } else {
        $config->{GridftpdEnabled} = 'no';
    }

    # global AdminDomain configuration
    if (defined $jsonconf->{'infosys/admindomain'}) {
        my $admindomain_options = { $jsonconf->{'infosys/admindomain'} };
        rename_keys $admindomain_options, $config->{'admindomain'}, {name => 'Name',
                                                    otherinfo => 'OtherInfo',
                                                    description => 'Description',
                                                    www => 'WWW',
                                                    distributed => 'Distributed',
                                                    owner => 'Owner'
                                                    };
        move_keys $admindomain_options, $config->{'admindomain'}, [keys %$admindomain_options];

    } else {
        $log->info('[infosys/admindomain] section missing. No site information will be published.');
    }

    ############################ legacy ini config file structure #############################

    move_keys $common, $config, ['AdminDomain'];

    my $cluster = $jsonconf->{'cluster'};
    if (%$cluster) {
        # Ignored: cluster_location, lrmsconfig
        rename_keys $cluster, $config, {arex_mount_point => 'endpoint'};
        rename_keys $cluster, $config->{location}, { cluster_location => 'PostCode' };
        rename_keys $cluster, $config->{service}, {
                                 interactive_contactstring => 'InteractiveContactstring',
                                 cluster_owner => 'ClusterOwner', localse => 'LocalSE',
                                 authorizedvo => 'AuthorizedVO', homogeneity => 'Homogeneous',
                                 architecture => 'Platform', opsys => 'OpSys', benchmark => 'Benchmark',
                                 nodememory => 'MaxVirtualMemory', middleware => 'Middleware',
                                 cluster_alias => 'ClusterAlias', comment => 'ClusterComment'};
        if ($cluster->{clustersupport} and $cluster->{clustersupport} =~ /(.*)@/) {
            my $contact = {};
            push @{$config->{contacts}}, $contact;
            $contact->{Name} = $1;
            $contact->{Detail} = "mailto:".$cluster->{clustersupport};
            $contact->{Type} = 'usersupport';
        }
        if (defined $cluster->{nodeaccess}) {
            $config->{service}{ConnectivityIn} = 0;
            $config->{service}{ConnectivityOut} = 0;
            for (split '\[separator\]', $cluster->{nodeaccess}) {
                $config->{service}{ConnectivityIn} = 1 if lc $_ eq 'inbound';
                $config->{service}{ConnectivityOut} = 1 if lc $_ eq 'outbound';
            }
        }
        move_keys $cluster, $config->{service}, [keys %$share_options, keys %$xenv_options];
        move_keys $cluster, $config, [keys %$lrms_options, keys %$lrms_share_options];
    }
    # TODO: parse queues with something similar to list_subsections
    # hash keys stripped of the queue/ prefix will do
    my @qnames=();
    for my $keyname (keys %{$jsonconf}) {
	   push(@qnames,$1) if $keyname =~ /queue\/(.*)/;
	}
    for my $name (@qnames) {
        my $queue = $jsonconf->{"queue/$name"};

        my $sconf = $config->{shares}{$name} ||= {};
        my $xeconf = $config->{xenvs}{$name} ||= {};
        push @{$sconf->{ExecutionEnvironmentName}}, $name;

        $log->error("MappingQuue option only allowed under ComputingShare section") if $queue->{MappingQuue};
        delete $queue->{MappingQueue};
        $log->error("ExecutionEnvironmentName option only allowed under ComputingShare section") if $queue->{ExecutionEnvironmentName};
        delete $queue->{ExecutionEnvironmentName};
        $log->error("NodeSelection option only allowed under ExecutionEnvironment section") if $queue->{NodeSelection};
        delete $queue->{NodeSelection};

        rename_keys $queue, $sconf, {scheduling_policy => 'SchedulingPolicy',
                                     nodememory => 'MaxVirtualMemory', comment => 'Description', maxslotsperjob => 'MaxSlotsPerJob'};
        move_keys $queue, $sconf, [keys %$share_options, keys %$lrms_share_options];

        rename_keys $queue, $xeconf, {homogeneity => 'Homogeneous', architecture => 'Platform',
                                      opsys => 'OpSys', benchmark => 'Benchmark'};
        move_keys $queue, $xeconf, [keys %$xenv_options];
        $xeconf->{NodeSelection} = {};
    }

    ################################# new ini config file structure ##############################
    ## TODO: these do not exist for real. The new ini was never used.
#~ 
    #~ my $provider = { $iniparser->get_section("InfoProvider") };
    #~ move_keys $provider, $config, ['debugLevel', 'ProviderLog', 'PublishNordugrid', 'AdminDomain'];
    #~ move_keys $provider, $config->{service}, [keys %{$config_schema->{service}}];
#~ 
    #~ my @gnames = $iniparser->list_subsections('ExecutionEnvironment');
    #~ for my $name (@gnames) {
        #~ my $xeconf = $config->{xenvs}{$name} ||= {};
        #~ my $section = { $iniparser->get_section("ExecutionEnvironment/$name") };
        #~ $xeconf->{NodeSelection} ||= {};
        #~ $xeconf->{NodeSelection}{Regex} = $section->{NodeSelectionRegex} if $section->{NodeSelectionRegex};
        #~ $xeconf->{NodeSelection}{Command} = $section->{NodeSelectionCommand} if $section->{NodeSelectionCommand};
        #~ $xeconf->{NodeSelection}{Tag} = $section->{NodeSelectionTag} if $section->{NodeSelectionTag};
        #~ move_keys $section, $xeconf, [keys %$xenv_options, 'OtherInfo'];
    #~ }
    #~ my @snames = $iniparser->list_subsections('ComputingShare');
    #~ for my $name (@snames) {
        #~ my $sconf = $config->{shares}{$name} ||= {};
        #~ my $section = { $iniparser->get_section("ComputingShare/$name") };
        #~ move_keys $section, $sconf, [keys %{$config_schema->{shares}{'*'}}];
    #~ }
    #~ my $location = { $iniparser->get_section("Location") };
    #~ $config->{location} = $location if %$location;
    #~ my @ctnames = $iniparser->list_subsections('Contact');
    #~ for my $name (@ctnames) {
        #~ my $section = { $iniparser->get_section("Contact/$name") };
        #~ push @{$config->{contacts}}, $section;
    #~ }

    # Create a list with all multi-valued options based on $config_schema.
    my @multival = ();
    hash_tree_apply $config_schema, sub { my $h = shift;
                                           for (keys %$h) {
                                               next if ref $h->{$_} ne 'ARRAY';
                                               next if ref $h->{$_}[0]; # exclude deep structures
                                               push @multival, $_;
                                           }
                                     };
    # Transform multi-valued options into arrays
    hash_tree_apply $config, sub { my $h = shift;
                                   while (my ($k,$v) = each %$h) {
                                       next if ref $v; # skip anything other than scalars
                                       $h->{$k} = [split '\[separator\]', $v];
                                       unless (grep {$k eq $_} @multival) {
                                           $h->{$k} = pop @{$h->{$k}}; # single valued options, remember last defined value only
                                       }
                                   }
                             };

    hash_tree_apply $config, sub { fixbools shift, $allbools };

    return $config;
}


#
# Check whether a file is XML
#
sub isXML {
    my $file = shift;
    $log->fatal("Can't open $file") unless open (CONFIGFILE, "<$file");
    my $isxml = 0;
    while (my $line = <CONFIGFILE>) {
        chomp $line;
        next unless $line;
        if ($line =~ m/^\s*<\?xml/) {$isxml = 1; last};
        if ($line =~ m/^\s*<!--/)   {$isxml = 1; last};
        last;
    }
    close CONFIGFILE;
    return $isxml;
}

#
# Grand config parser for A-REX. It can parse INI and XML config files. When
# parsing an XML config, it checks for the gmconfig option and parses the
# linked INI file too, merging the 2 configs. Options defined in the INI
# file override the ones from the XML file.
#
sub parseConfig {
    my ($file,$arc_location) = @_;
    my $config;
    #if (isXML($file)) {
    #    $config = build_config_from_xmlfile($file, $arc_location);
    #    my $inifile = $config->{gmconfig};
    #    $config = build_config_from_inifile($inifile, $config) if $inifile;
    #} else {
    #    $config = build_config_from_inifile($file);
    #}
    
    $config = build_config_from_json($file);

    #print Dumper($config);

    # C 134
    LogUtils::level($config->{arex}{loglevel}) if $config->{arex}{loglevel};

    my $checker = InfoChecker->new($config_schema);
    my @messages = $checker->verify($config,1);
    $log->verbose("config key config->$_") foreach @messages;
    $log->verbose("Some required config options are missing") if @messages;

    return $config;
}

################### support for shell scripts ############################

{
    my $nb;

    sub _print_shell_start { my $nb = 0 }
    sub _print_shell_end { print "_CONFIG_NUM_BLOCKS=$nb\n" }

    sub _print_shell_section {
        my ($bn,$opts) = @_;
        $nb++;
        my $prefix = "_CONFIG_BLOCK$nb";

        print $prefix."_NAME=\Q$bn\E\n";
        my $no=0;
        while (my ($opt,$val)=each %$opts) {
            unless ($opt =~ m/^\w+$/) {
                print "echo config_parser: Skipping malformed option \Q$opt\E 1>&2\n";
                next;
            }
            if (not ref $val) {
                $no++;
                $val = '' if not defined $val;
                print $prefix."_OPT${no}_NAME=$opt\n";
                print $prefix."_OPT${no}_VALUE=\Q$val\E\n";
            } elsif (ref $val eq 'ARRAY') {
                # multi-valued option
                for (my $i=0; $i<@$val; $i++) {
                    $no++;
                    $val->[$i] = '' if not defined $val->[$i];
                    print $prefix."_OPT${no}_NAME=$opt"."_".($i+1)."\n";
                    print $prefix."_OPT${no}_VALUE=\Q@{[$val->[$i]]}\E\n";
                }
            }
        }
        print $prefix."_NUM=$no\n";
    }
}

#
# Reads A-REX config and prints out configuration options for LRMS control
# scripts. Only the LRMS-related options are handled. The output is executable
# shell script meant to be sourced by 'config_parser.sh'.
#
sub printLRMSConfigScript {
    my $file = shift;
    my $config = parseConfig($file);

    _print_shell_start();

    my $common = {};
    move_keys $config, $common, [keys %$lrms_options, keys %$lrms_share_options];

    _print_shell_section('common', $common);

    my $gmopts = {};
    $gmopts->{runtimedir} = $config->{runtimedir} if $config->{runtimedir};
    $gmopts->{gnu_time} = $config->{gnu_time} if $config->{gnu_time};
    $gmopts->{scratchdir} = $config->{scratchdir} if $config->{scratchdir};
    $gmopts->{shared_scratch} = $config->{shared_scratch} if $config->{shared_scratch};
    # shared_filesystem: if not set, assume 'yes'
    $gmopts->{shared_filesystem} = $config->{shared_filesystem} if $config->{shared_filesystem};

    _print_shell_section('grid-manager', $gmopts);

    my $cluster = {};
    rename_keys $config->{service}, $cluster, {MaxVirtualMemory => 'nodememory'};
    move_keys $config->{service}, $cluster, ['defaultmemory'];

    _print_shell_section('cluster', $cluster) if %$cluster;

    for my $sname (keys %{$config->{shares}}) {
        my $queue = {};
        move_keys $config->{shares}{$sname}, $queue, [keys %$lrms_options, keys %$lrms_share_options];
        rename_keys $config->{shares}{$sname}, $queue, {MaxVirtualMemory => 'nodememory'};

        my $qname = $config->{shares}{$sname}{MappingQueue};
        $queue->{MappingQueue} = $qname if $qname;

        _print_shell_section("queue/$sname", $queue);
    }

    _print_shell_end();
}

## getValueOf: Cherry picks arc.conf values
## Perl wrapper for the python parser
## input: configfile,configblock, configoption
sub getValueOf ($$$){
   	my ($arcconf,$block,$option) = @_;	
	
    # get the calling script basepath. Will be used to
    # find external scripts like arcconfig-parser.
    my $libexecpath = dirname($0);	
	
	my $value='';
	{ 
      local $/; # slurp mode
	  open (my $parserout, "$libexecpath/arcconfig-parser -c $arcconf -b $block -o $option |") || $log->error("Python config parser error: $! at line: ".__LINE__." libexecpath: $libexecpath");
	  $value = <$parserout>;
	  close $parserout;
	}

    # strip trailing newline
    chomp $value;
  
    return $value;

}

sub dumpInternalDatastructure ($){
	my ($config) = @_;
    print Dumper($config);
}

1;

__END__
