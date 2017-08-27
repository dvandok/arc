# set environment variables:
#   SGE_BIN_PATH
#   SGE_ROOT
#   SGE_CELL
#   SGE_QMASTER_PORT
#   SGE_EXECD_PORT
#

##############################################################
# Read ARC config file
##############################################################

if [ -z "$pkgdatadir" ]; then echo 'pkgdatadir must be set' 1>&2; exit 1; fi

. "$pkgdatadir/config_parser_compat.sh" || exit $?

ARC_CONFIG=${ARC_CONFIG:-/etc/arc.conf}
config_parse_file $ARC_CONFIG 1>&2 || exit $?

config_import_section "common"
config_import_section "infosys"
config_import_section "arex"

# Also read queue section
if [ ! -z "$joboption_queue" ]; then
  config_import_section "queue/$joboption_queue"
fi

# performance logging: if perflogdir or perflogfile is set, logging is turned on. So only set them when enable_perflog_reporting is ON
unset perflogdir
unset perflogfile
enable_perflog=${CONFIG_enable_perflog_reporting:-no}
if [ "$CONFIG_enable_perflog_reporting" == "expert-debug-on" ]; then
   perflogdir=${CONFIG_perflogdir:-/var/log/arc/perfdata}
   perflogfile="${perflogdir}/backends.perflog"
fi

##############################################################
# Initialize SGE environment variables
##############################################################

SGE_ROOT=${CONFIG_sge_root:-$SGE_ROOT}

if [ -z "$SGE_ROOT" ]; then
    echo 'SGE_ROOT not set' 1>&2
    return 1
fi

SGE_CELL=${SGE_CELL:-default}
SGE_CELL=${CONFIG_sge_cell:-$SGE_CELL}
export SGE_ROOT SGE_CELL

if [ ! -z "$CONFIG_sge_qmaster_port" ]; then
    export SGE_QMASTER_PORT=$CONFIG_sge_qmaster_port
fi

if [ ! -z "$CONFIG_sge_execd_port" ]; then
    export SGE_EXECD_PORT=$CONFIG_sge_execd_port
fi

##############################################################
# Find path to SGE executables
##############################################################

# 1. use sge_bin_path config option, if set
if [ ! -z "$CONFIG_sge_bin_path" ]; then
    SGE_BIN_PATH=$CONFIG_sge_bin_path;
fi

# 2. otherwise see if qsub can be found in the path
if [ -z "$SGE_BIN_PATH" ]; then
    qsub=$(type -p qsub)
    SGE_BIN_PATH=${qsub%/*}
    unset qsub
fi

if [ ! -x "$SGE_BIN_PATH/qsub" ]; then
    echo 'SGE executables not found! Check that sge_bin_path is defined' 1>&2
    return 1
fi

export SGE_BIN_PATH
