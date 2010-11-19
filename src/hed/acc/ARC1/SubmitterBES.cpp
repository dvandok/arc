// -*- indent-tabs-mode: nil -*-

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string>
#include <sstream>

#include <arc/StringConv.h>
#include <arc/UserConfig.h>
#include <arc/client/ExecutionTarget.h>
#include <arc/client/Job.h>
#include <arc/client/JobDescription.h>
#include <arc/message/MCC.h>

#include "SubmitterBES.h"
#include "AREXClient.h"

namespace Arc {

  Logger SubmitterBES::logger(Logger::getRootLogger(), "Submitter.BES");

  static std::string char_to_hex(char v) {
    std::string s;
    unsigned int n;
    n = (unsigned int)((v & 0xf0) >> 4);
    if(n < 10) { s+=(char)('0'+n); } else { s+=(char)('a'+n-10); }
    n = (unsigned int)(v & 0x0f);
    if(n < 10) { s+=(char)('0'+n); } else { s+=(char)('a'+n-10); }
    return s;
  }

  static std::string disguise_id_into_url(const URL& u,const std::string& id) {
    std::string jobid = u.str();
    jobid+="#";
    for(std::string::size_type p = 0;p < id.length();++p) {
      jobid+=char_to_hex(id[p]);
    }
    return jobid;
  }

  SubmitterBES::SubmitterBES(const UserConfig& usercfg)
    : Submitter(usercfg, "BES") {}

  SubmitterBES::~SubmitterBES() {}

  Plugin* SubmitterBES::Instance(PluginArgument *arg) {
    SubmitterPluginArgument *subarg =
      dynamic_cast<SubmitterPluginArgument*>(arg);
    if (!subarg)
      return NULL;
    return new SubmitterBES(*subarg);
  }

  bool SubmitterBES::Submit(const JobDescription& jobdesc,
                            const ExecutionTarget& et, Job& job) const {
    MCCConfig cfg;
    usercfg.ApplyToConfig(cfg);
    AREXClient ac(et.url, cfg, usercfg.Timeout(), false);

    std::string jobid;
    // !! TODO: ordinary JSDL is needed - keeping ARCJSDL so far
    if (!ac.submit(jobdesc.UnParse("ARCJSDL"), jobid, et.url.Protocol() == "https"))
      return false;

    if (jobid.empty()) {
      logger.msg(INFO, "No job identifier returned by BES service");
      return false;
    }

    XMLNode xJobid(jobid);

    // Unfortunately Job handling framework somewhy want to have job
    // URL instead of identifier we have to invent one. So we disguise
    // XML blob inside URL path.
    AddJobDetails(jobdesc, URL(disguise_id_into_url(et.url,xJobid)), et.Cluster, et.url, job);

    return true;
  }

  bool SubmitterBES::Migrate(const URL& /* jobid */, const JobDescription& /* jobdesc */,
                             const ExecutionTarget& et, bool /* forcemigration */,
                             Job& /* job */) const {
    logger.msg(INFO, "Trying to migrate to %s: Migration to a BES resource is not supported.", et.url.str());
    return false;
  }

  bool SubmitterBES::ModifyJobDescription(JobDescription& /* jobdesc */, const ExecutionTarget& /* et */) const {
    return true;
  }

} // namespace Arc
