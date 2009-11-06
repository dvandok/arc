#ifndef __GM_CONFIG_PRE_H__
#define __GM_CONFIG_PRE_H__

#include <string>
#include <list>

#include <arc/XMLNode.h>

class ContinuationPlugins;
class RunPlugin;

/*
  Reads configuration file to find directories associated with
  given user.
  Accepts:
    my_username - username for which directories will be searched.
  Returns
    true - success
    false - failure
    On success 'control_dir', 'session_root', 'default_lrms' and
    'default_queue' are filled. For meaning of these directories
    look description of grid-manager and configuration template.
*/
bool configure_user_dirs(const std::string &my_username,
                std::string &control_dir,std::string &session_root,
                std::string &default_lrms,std::string &default_queue,
                std::list<std::string>& queues,
                ContinuationPlugins &plugins,RunPlugin& cred,
                std::string& allow_submit,bool& strict_session);

/*
  Reads configuration file to find all defined control directories.
  Returns
    true - success
    false - failure
    On success 'control_dirs' contains list of all control directories.
*/
bool configure_control_dirs(std::list<std::string>& control_dirs);
bool configure_control_dirs(Arc::XMLNode cfg,std::list<std::string>& control_dirs);

#endif // __GM_CONFIG_PRE_H__
