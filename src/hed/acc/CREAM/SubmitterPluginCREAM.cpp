// -*- indent-tabs-mode: nil -*-

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <arc/GUID.h>
#include <arc/StringConv.h>
#include <arc/UserConfig.h>
#include <arc/compute/ExecutionTarget.h>
#include <arc/compute/Job.h>
#include <arc/compute/JobDescription.h>
#include <arc/compute/SubmissionStatus.h>
#include <arc/message/MCC.h>

#include "CREAMClient.h"
#include "SubmitterPluginCREAM.h"

namespace Arc {

  bool SubmitterPluginCREAM::isEndpointNotSupported(const std::string& endpoint) const {
    const std::string::size_type pos = endpoint.find("://");
    return pos != std::string::npos && lower(endpoint.substr(0, pos)) != "http" && lower(endpoint.substr(0, pos)) != "https";
  }

  SubmissionStatus SubmitterPluginCREAM::Submit(const std::list<JobDescription>& jobdescs, const std::string& endpoint, EntityConsumer<Job>& jc, std::list<const JobDescription*>& notSubmitted) {
    MCCConfig cfg;
    usercfg.ApplyToConfig(cfg);
    
    URL url((endpoint.find("://") == std::string::npos ? "https://" : "") + endpoint, false, 8443);
    URL infourl("ldap://" + url.Host(), false, 2170, "/o=grid");

    SubmissionStatus retval;
    for (std::list<JobDescription>::const_iterator it = jobdescs.begin(); it != jobdescs.end(); ++it) {
      std::string delegationid = UUID();
      URL delegationurl(url);
      delegationurl.ChangePath(delegationurl.Path() + "/gridsite-delegation");
      CREAMClient gLiteClientDelegation(delegationurl, cfg, usercfg.Timeout());
      if (!gLiteClientDelegation.createDelegation(delegationid, usercfg.ProxyPath())) {
        logger.msg(INFO, "Failed creating signed delegation certificate");
        notSubmitted.push_back(&*it);
        retval |= SubmissionStatus::DESCRIPTION_NOT_SUBMITTED;
        retval |= SubmissionStatus::AUTHENTICATION_ERROR;
        continue;
      }
      URL submissionurl(url);
      submissionurl.ChangePath(submissionurl.Path() + "/CREAM2");
      CREAMClient gLiteClientSubmission(submissionurl, cfg, usercfg.Timeout());
      gLiteClientSubmission.setDelegationId(delegationid);
  
      JobDescription preparedjobdesc(*it);
      if (!preparedjobdesc.Prepare()) {
        logger.msg(INFO, "Failed to prepare job description to target resources");
        notSubmitted.push_back(&*it);
        retval |= SubmissionStatus::DESCRIPTION_NOT_SUBMITTED;
        continue;
      }

      std::string jobdescstring;
      if (!preparedjobdesc.UnParse(jobdescstring, "egee:jdl")) {
        logger.msg(INFO, "Unable to submit job. Job description is not valid in the %s format", "egee:jdl");
        notSubmitted.push_back(&*it);
        retval |= SubmissionStatus::DESCRIPTION_NOT_SUBMITTED;
        continue;
      }
  
      creamJobInfo jobInfo;
      if (!gLiteClientSubmission.registerJob(jobdescstring, jobInfo)) {
        logger.msg(INFO, "Failed registering job");
        notSubmitted.push_back(&*it);
        retval |= SubmissionStatus::DESCRIPTION_NOT_SUBMITTED;
        retval |= SubmissionStatus::ERROR_FROM_ENDPOINT;
        continue;
      }
  
      if (!PutFiles(preparedjobdesc, jobInfo.ISB)) {
        logger.msg(INFO, "Failed uploading local input files");
        notSubmitted.push_back(&*it);
        retval |= SubmissionStatus::DESCRIPTION_NOT_SUBMITTED;
        retval |= SubmissionStatus::ERROR_FROM_ENDPOINT;
        continue;
      }
  
      if (!gLiteClientSubmission.startJob(jobInfo.id)) {
        logger.msg(INFO, "Failed starting job");
        notSubmitted.push_back(&*it);
        retval |= SubmissionStatus::DESCRIPTION_NOT_SUBMITTED;
        retval |= SubmissionStatus::ERROR_FROM_ENDPOINT;
        continue;
      }
  
      Job j;
      j.JobID = submissionurl.str() + '/' + jobInfo.id;
      j.ServiceInformationURL = infourl;
      j.ServiceInformationURL.ChangeLDAPFilter("");
      j.ServiceInformationInterfaceName = "org.nordugrid.ldapng";
      j.JobStatusURL = url;
      j.JobStatusInterfaceName = "org.glite.ce.cream";
      j.JobManagementURL = url;
      j.JobManagementInterfaceName = "org.glite.ce.cream";
      j.StageInDir = jobInfo.ISB;
      j.StageOutDir = jobInfo.OSB;
      AddJobDetails(preparedjobdesc, j);
      j.IDFromEndpoint = jobInfo.id;
      jc.addEntity(j);
    }

    return retval;
  }

  SubmissionStatus SubmitterPluginCREAM::Submit(const std::list<JobDescription>& jobdescs, const ExecutionTarget& et, EntityConsumer<Job>& jc, std::list<const JobDescription*>& notSubmitted) {
    MCCConfig cfg;
    usercfg.ApplyToConfig(cfg);
    URL url(et.ComputingEndpoint->URLString);

    SubmissionStatus retval;
    for (std::list<JobDescription>::const_iterator it = jobdescs.begin(); it != jobdescs.end(); ++it) {
      std::string delegationid = UUID();
      URL delegationurl(url);
      delegationurl.ChangePath(delegationurl.Path() + "/gridsite-delegation");
      CREAMClient gLiteClientDelegation(delegationurl, cfg, usercfg.Timeout());
      if (!gLiteClientDelegation.createDelegation(delegationid, usercfg.ProxyPath())) {
        logger.msg(INFO, "Failed creating singed delegation certificate");
        notSubmitted.push_back(&*it);
        retval |= SubmissionStatus::DESCRIPTION_NOT_SUBMITTED;
        retval |= SubmissionStatus::AUTHENTICATION_ERROR;
        continue;
      }
      URL submissionurl(url);
      submissionurl.ChangePath(submissionurl.Path() + "/CREAM2");
      CREAMClient gLiteClientSubmission(submissionurl, cfg, usercfg.Timeout());
      gLiteClientSubmission.setDelegationId(delegationid);
  
      JobDescription preparedjobdesc(*it);
      if (!preparedjobdesc.Prepare(et)) {
        logger.msg(INFO, "Failed to prepare job description to target resources");
        notSubmitted.push_back(&*it);
        retval |= SubmissionStatus::DESCRIPTION_NOT_SUBMITTED;
        continue;
      }
  
      if (preparedjobdesc.OtherAttributes.find("egee:jdl;BatchSystem") == preparedjobdesc.OtherAttributes.end()) {
        if (!et.ComputingManager->ProductName.empty()) {
          preparedjobdesc.OtherAttributes["egee:jdl;BatchSystem"] = et.ComputingManager->ProductName;
        }
        else if (!et.ComputingShare->MappingQueue.empty()) {
          preparedjobdesc.OtherAttributes["egee:jdl;BatchSystem"] = et.ComputingShare->MappingQueue;
        }
      }
  
      std::string jobdescstring;
      if (!preparedjobdesc.UnParse(jobdescstring, "egee:jdl")) {
        logger.msg(INFO, "Unable to submit job. Job description is not valid in the %s format", "egee:jdl");
        notSubmitted.push_back(&*it);
        retval |= SubmissionStatus::DESCRIPTION_NOT_SUBMITTED;
        continue;
      }
  
      creamJobInfo jobInfo;
      if (!gLiteClientSubmission.registerJob(jobdescstring, jobInfo)) {
        logger.msg(INFO, "Failed registering job");
        notSubmitted.push_back(&*it);
        retval |= SubmissionStatus::DESCRIPTION_NOT_SUBMITTED;
        retval |= SubmissionStatus::ERROR_FROM_ENDPOINT;
        continue;
      }
  
      if (!PutFiles(preparedjobdesc, jobInfo.ISB)) {
        logger.msg(INFO, "Failed uploading local input files");
        notSubmitted.push_back(&*it);
        retval |= SubmissionStatus::DESCRIPTION_NOT_SUBMITTED;
        retval |= SubmissionStatus::ERROR_FROM_ENDPOINT;
        continue;
      }
  
      if (!gLiteClientSubmission.startJob(jobInfo.id)) {
        logger.msg(INFO, "Failed starting job");
        notSubmitted.push_back(&*it);
        retval |= SubmissionStatus::DESCRIPTION_NOT_SUBMITTED;
        retval |= SubmissionStatus::ERROR_FROM_ENDPOINT;
        continue;
      }

      // Prepare contact url for information about this job
      URL infoendpoint;
      for (std::list< CountedPointer<ComputingEndpointAttributes> >::const_iterator it = et.OtherEndpoints.begin(); it != et.OtherEndpoints.end(); it++) {
        if ((*it)->InterfaceName == "org.nordugrid.ldapglue1") {
          infoendpoint = URL((*it)->URLString);
          infoendpoint.ChangeLDAPScope(URL::subtree);
        }
      }

      Job j;
      j.JobID = submissionurl.str() + '/' + jobInfo.id;
      j.ServiceInformationURL = infoendpoint;
      j.ServiceInformationURL.ChangeLDAPFilter("");
      j.ServiceInformationInterfaceName = "org.nordugrid.ldapglue1";
      j.JobStatusURL = url;
      j.JobStatusInterfaceName = "org.glite.ce.cream";
      j.JobManagementURL = url;
      j.JobManagementInterfaceName = "org.glite.ce.cream";
      j.StageInDir = jobInfo.ISB;
      j.StageOutDir = jobInfo.OSB;
      AddJobDetails(preparedjobdesc, j);
      j.IDFromEndpoint = jobInfo.id;
      jc.addEntity(j);
    }

    return retval;
  }
} // namespace Arc
