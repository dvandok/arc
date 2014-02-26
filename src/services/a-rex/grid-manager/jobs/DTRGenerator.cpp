#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/stat.h>
#include <fcntl.h>

#include <arc/FileUtils.h>
#include <arc/FileAccess.h>
#include <arc/data/FileCache.h>

#include "../files/Delete.h"
#include "../conf/UrlMapConfig.h"
#include "../files/ControlFileHandling.h"
#include "../conf/StagingConfig.h"

#include "GMJob.h"

#include "DTRGenerator.h"

namespace ARex {

Arc::Logger DTRInfo::logger(Arc::Logger::getRootLogger(), "DTRInfo");

DTRInfo::DTRInfo(const GMConfig& config): config(config) {
}

void DTRInfo::receiveDTR(DataStaging::DTR_ptr dtr) {
  // write state info to job.id.input for example
}

Arc::Logger DTRGenerator::logger(Arc::Logger::getRootLogger(), "Generator");

bool compare_job_description(GMJob first, GMJob second) {
  return first.get_local()->priority > second.get_local()->priority;
}

void DTRGenerator::main_thread(void* arg) {
  ((DTRGenerator*)arg)->thread();
}

void DTRGenerator::thread() {

  // set up logging - to avoid logging DTR logs to the main A-REX log
  // we disconnect the root logger while submitting to the Scheduler
  Arc::Logger::getRootLogger().setThreadContext();

  while (generator_state != DataStaging::TO_STOP) {
    // look at event queue and deal with any events.
    // This method of iteration should be thread-safe because events
    // are always added to the end of the list

    // take cancelled jobs first so we can ignore other DTRs in those jobs
    event_lock.lock();
    std::list<std::string>::iterator it_cancel = jobs_cancelled.begin();
    while (it_cancel != jobs_cancelled.end()) {
      event_lock.unlock();
      processCancelledJob(*it_cancel);
      event_lock.lock();
      it_cancel = jobs_cancelled.erase(it_cancel);
    }

    // next DTRs sent back from the Scheduler
    std::list<DataStaging::DTR_ptr>::iterator it_dtrs = dtrs_received.begin();
    while (it_dtrs != dtrs_received.end()) {
      event_lock.unlock();
      processReceivedDTR(*it_dtrs);
      event_lock.lock();
      // delete DTR LogDestinations
      (*it_dtrs)->get_logger()->deleteDestinations();

      it_dtrs = dtrs_received.erase(it_dtrs);
    }

    // finally new jobs
    std::list<GMJob>::iterator it_jobs = jobs_received.begin();
    // it can happen that the list grows faster than the jobs are processed
    // so here we only process for a small time to avoid blocking other
    // jobs finishing
    Arc::Time limit(Arc::Time() + Arc::Period(30));

    // sort the list by job priority
    jobs_received.sort(compare_job_description);
    while (it_jobs != jobs_received.end() && Arc::Time() < limit) {
      event_lock.unlock();
      processReceivedJob(*it_jobs);
      event_lock.lock();
      it_jobs = jobs_received.erase(it_jobs);
    }
    event_lock.unlock();
    Glib::usleep(50000);
  }
  // stop scheduler - cancels all DTRs and waits for them to complete
  scheduler->stop();
  // Handle all the DTRs returned by the scheduler, in case there are completed
  // DTRs to process before exiting and thus avoiding redoing those transfers
  // when A-REX restarts.
  // Lock is not necessary here because scheduler has finished and A-REX is
  // waiting for this thread to exit.
  std::list<DataStaging::DTR_ptr>::iterator it_dtrs = dtrs_received.begin();
  while (it_dtrs != dtrs_received.end()) {
    processReceivedDTR(*it_dtrs);
    // delete DTR LogDestinations
    (*it_dtrs)->get_logger()->deleteDestinations();
    it_dtrs = dtrs_received.erase(it_dtrs);
  }
  run_condition.signal();
  logger.msg(Arc::INFO, "Exiting Generator thread");
}

DTRGenerator::DTRGenerator(const GMConfig& config,
                           void (*kicker_func)(void*),
                           void* kicker_arg) :
    generator_state(DataStaging::INITIATED),
    config(config),
    staging_conf(config),
    info(config),
    kicker_func(kicker_func),
    kicker_arg(kicker_arg) {

  if (!staging_conf) return;
  // Set log level for DTR in job.id.errors files
  DataStaging::DTR::LOG_LEVEL = staging_conf.log_level;

  scheduler = DataStaging::Scheduler::getInstance();

  // Convert A-REX configuration values to DTR configuration

  // If not configured, set the DTR dump file to the first control dir registered
  std::string dtr_log(staging_conf.dtr_log);
  if (dtr_log.empty()) dtr_log = config.ControlDir()+"/dtrstate.log";
  scheduler->SetDumpLocation(dtr_log);

  // Read DTR state from previous dump to find any transfers stopped half-way
  // If those destinations appear again, add overwrite=yes
  readDTRState(dtr_log);

  // Processing limits
  scheduler->SetSlots(staging_conf.max_processor,
                      staging_conf.max_processor,
                      staging_conf.max_delivery,
                      staging_conf.max_emergency,
                      staging_conf.max_prepared);

  // Transfer shares
  DataStaging::TransferSharesConf share_conf(staging_conf.share_type,
                                             staging_conf.defined_shares);
  scheduler->SetTransferSharesConf(share_conf);

  // Transfer limits
  DataStaging::TransferParameters transfer_limits;
  transfer_limits.min_current_bandwidth = staging_conf.min_speed;
  transfer_limits.averaging_time = staging_conf.min_speed_time;
  transfer_limits.min_average_bandwidth = staging_conf.min_average_speed;
  transfer_limits.max_inactivity_time = staging_conf.max_inactivity_time;
  scheduler->SetTransferParameters(transfer_limits);

  // URL mappings
  UrlMapConfig url_map(config);
  scheduler->SetURLMapping(url_map);

  // Preferred pattern
  scheduler->SetPreferredPattern(staging_conf.preferred_pattern);

  // Delivery services
  scheduler->SetDeliveryServices(staging_conf.delivery_services);

  // Limit on remote delivery size
  scheduler->SetRemoteSizeLimit(staging_conf.remote_size_limit);

  // End of configuration - start Scheduler thread
  scheduler->start();

  generator_state = DataStaging::RUNNING;
  Arc::CreateThreadFunction(&main_thread, this);
}

DTRGenerator::~DTRGenerator() {
  if (generator_state != DataStaging::RUNNING)
    return;
  generator_state = DataStaging::TO_STOP;
  run_condition.wait();
  generator_state = DataStaging::STOPPED;
}

void DTRGenerator::receiveDTR(DataStaging::DTR_ptr dtr) {

  if (generator_state == DataStaging::INITIATED || generator_state == DataStaging::STOPPED) {
    logger.msg(Arc::ERROR, "DTRGenerator is not running!");
    return;
  } else if (generator_state == DataStaging::TO_STOP) {
    logger.msg(Arc::VERBOSE, "Received DTR %s during Generator shutdown - may not be processed", dtr->get_id());
    // still a chance to process this DTR so don't return
  }
  event_lock.lock();
  dtrs_received.push_back(dtr);
  event_lock.unlock();
}

void DTRGenerator::receiveJob(const GMJob& job) {

  if (generator_state != DataStaging::RUNNING) {
    logger.msg(Arc::WARNING, "DTRGenerator is not running!");
  }

  // Add to jobs list even if Generator is stopped, so that A-REX doesn't
  // think that staging has finished.
  event_lock.lock();
  jobs_received.push_back(job);
  event_lock.unlock();
}

void DTRGenerator::cancelJob(const GMJob& job) {

  if (generator_state != DataStaging::RUNNING) {
    logger.msg(Arc::WARNING, "DTRGenerator is not running!");
  }

  event_lock.lock();
  jobs_cancelled.push_back(job.get_id());
  event_lock.unlock();
}

bool DTRGenerator::queryJobFinished(GMJob& job) {

  // Data staging is finished if the job is in finished_jobs and
  // not in active_dtrs or jobs_received.

  // check if this job is still in the received jobs queue
  event_lock.lock();
  for (std::list<GMJob>::iterator i = jobs_received.begin(); i != jobs_received.end(); ++i) {
    if (*i == job) {
      event_lock.unlock();
      return false;
    }
  }
  event_lock.unlock();

  // check if any DTRs in this job are still active
  lock.lock();
  if (active_dtrs.find(job.get_id()) != active_dtrs.end()) {
    lock.unlock();
    return false;
  }
  std::map<std::string, std::string>::iterator i = finished_jobs.find(job.get_id());
  if (i != finished_jobs.end() && !i->second.empty()) {
    // add failure to job if any DTR failed
    job.AddFailure(i->second);
    finished_jobs[job.get_id()] = "";
  }
  lock.unlock();
  return true;
}

bool DTRGenerator::hasJob(const GMJob& job) {

  // check if this job is still in the received jobs queue
  event_lock.lock();
  for (std::list<GMJob>::iterator i = jobs_received.begin(); i != jobs_received.end(); ++i) {
    if (*i == job) {
      event_lock.unlock();
      return true;
    }
  }
  event_lock.unlock();

  // check if any DTRs in this job are still active
  lock.lock();
  if (active_dtrs.find(job.get_id()) != active_dtrs.end()) {
    lock.unlock();
    return true;
  }

  // finally check finished jobs
  std::map<std::string, std::string>::iterator i = finished_jobs.find(job.get_id());
  if (i != finished_jobs.end()) {
    lock.unlock();
    return true;
  }
  lock.unlock();
  // not found
  return false;
}

void DTRGenerator::removeJob(const GMJob& job) {

  // check if this job is still in the received jobs queue
  event_lock.lock();
  for (std::list<GMJob>::iterator i = jobs_received.begin(); i != jobs_received.end(); ++i) {
    if (*i == job) {
      event_lock.unlock();
      logger.msg(Arc::WARNING, "%s: Trying to remove job from data staging which is still active", job.get_id());
      return;
    }
  }
  event_lock.unlock();

  // check if any DTRs in this job are still active
  lock.lock();
  if (active_dtrs.find(job.get_id()) != active_dtrs.end()) {
    lock.unlock();
    logger.msg(Arc::WARNING, "%s: Trying to remove job from data staging which is still active", job.get_id());
    return;
  }

  // finally check finished jobs
  std::map<std::string, std::string>::iterator i = finished_jobs.find(job.get_id());
  if (i == finished_jobs.end()) {
    // warn if not in finished
    lock.unlock();
    logger.msg(Arc::WARNING, "%s: Trying remove job from data staging which does not exist", job.get_id());
    return;
  }
  finished_jobs.erase(i);
  lock.unlock();
}

bool DTRGenerator::processReceivedDTR(DataStaging::DTR_ptr dtr) {

  std::string jobid(dtr->get_parent_job_id());
  if (!(*dtr)) {
    logger.msg(Arc::ERROR, "%s: Invalid DTR", jobid);
    if (dtr->get_status() != DataStaging::DTRStatus::CANCELLED) {
      scheduler->cancelDTRs(jobid);
      lock.lock();
      finished_jobs[jobid] = std::string("Invalid Data Transfer Request");
      active_dtrs.erase(jobid);
      lock.unlock();
    }
    return false;
  }
  logger.msg(Arc::DEBUG, "%s: Received DTR %s to copy file %s in state %s",
                          jobid, dtr->get_id(), dtr->get_source()->str(), dtr->get_status().str());

  uid_t job_uid = config.StrictSession() ? dtr->get_local_user().get_uid() : 0;
  uid_t job_gid = config.StrictSession() ? dtr->get_local_user().get_gid() : 0;

  // Get session dir from .local if possible
  std::string session_dir;
  JobLocalDescription job_desc;
  if (job_local_read_file(jobid, config, job_desc) && !job_desc.sessiondir.empty()) {
    session_dir = job_desc.sessiondir;
  } else {
    logger.msg(Arc::WARNING, "%s: Failed reading local information", jobid);
    session_dir = config.SessionRoot(jobid) + '/' + jobid;
  }

  // create JobDescription object to pass to job_..write_file methods
  GMJob job(jobid, dtr->get_local_user(), session_dir);
  std::string dtr_transfer_statistics;
  
  if (dtr->error() && dtr->get_status() != DataStaging::DTRStatus::CANCELLED) {
    // for uploads, report error but let other transfers continue
    // for downloads, cancel all other transfers
    logger.msg(Arc::ERROR, "%s: DTR %s to copy file %s failed",
               jobid, dtr->get_id(), dtr->get_source()->str());

    lock.lock();
    if (!dtr->get_source()->Local() && finished_jobs.find(jobid) == finished_jobs.end()) { // download
      // cancel other DTRs and erase from our list unless error was already reported
      logger.msg(Arc::INFO, "%s: Cancelling other DTRs", jobid);
      scheduler->cancelDTRs(jobid);
    }
    // add error to finished jobs
    finished_jobs[jobid] += std::string("Failed in data staging: " + dtr->get_error_status().GetDesc() + '\n');
    lock.unlock();
  }
  else if (dtr->get_status() != DataStaging::DTRStatus::CANCELLED) {
    // remove from job.id.input/output files on success
    // find out if download or upload by checking which is remote file
    std::list<FileData> files;

    if (dtr->get_source()->Local()) {
      // output files
      dtr_transfer_statistics = "outputfile:url=" + dtr->get_destination()->str() + ',';

      if (!job_output_read_file(jobid, config, files)) {
        logger.msg(Arc::WARNING, "%s: Failed to read list of output files", jobid);
      } else {
        FileData uploaded_file;
        // go through list and take out this file
        for (std::list<FileData>::iterator i = files.begin(); i != files.end();) {
          // compare 'standard' URLs
          Arc::URL file_lfn(i->lfn);
          Arc::URL dtr_lfn(dtr->get_destination()->str());

          // check if it is in a dynamic list - if so remove from it
          if (i->pfn.size() > 1 && i->pfn[1] == '@') {
            std::string dynamic_output(session_dir+'/'+i->pfn.substr(2));
            std::list<FileData> dynamic_files;
            if (!job_Xput_read_file(dynamic_output, dynamic_files, job_uid, job_gid)) {
              logger.msg(Arc::WARNING, "%s: Failed to read dynamic output files in %s", jobid, dynamic_output);
            } else {
              logger.msg(Arc::DEBUG, "%s: Going through files in list %s", jobid, dynamic_output);
              for (std::list<FileData>::iterator dynamic_file = dynamic_files.begin();
                   dynamic_file != dynamic_files.end(); ++dynamic_file) {
                if (Arc::URL(dynamic_file->lfn).str() == dtr_lfn.str()) {
                  logger.msg(Arc::DEBUG, "%s: Removing %s from dynamic output file %s", jobid, dtr_lfn.str(), dynamic_output);
                  uploaded_file = *dynamic_file;
                  dynamic_files.erase(dynamic_file);
                  if (!job_Xput_write_file(dynamic_output, dynamic_files, job_output_all, job_uid, job_gid))
                    logger.msg(Arc::WARNING, "%s: Failed to write back dynamic output files in %s", jobid, dynamic_output);
                  break;
                }
              }
            }
          }
          if (file_lfn.str() == dtr_lfn.str()) {
            uploaded_file = *i;
            i = files.erase(i);
          } else {
            ++i;
          }
        }

        // write back .output file
        if (!job_output_write_file(job, config, files)) {
          logger.msg(Arc::WARNING, "%s: Failed to write list of output files", jobid);
        }
        if(!uploaded_file.pfn.empty()) {
          if(!job_output_status_add_file(job, config, uploaded_file)) {
            logger.msg(Arc::WARNING, "%s: Failed to write list of output status files", jobid);
          }
        }
      }
      if (dtr->get_source()->CheckSize()) dtr_transfer_statistics += "size=" + Arc::tostring(dtr->get_source()->GetSize()) + ',';
      dtr_transfer_statistics += "starttime=" + dtr->get_creation_time().str(Arc::UTCTime) + ',';
      dtr_transfer_statistics += "endtime=" + Arc::Time().str(Arc::UTCTime); 
    }
    else if (dtr->get_destination()->Local()) {
      // input files
      dtr_transfer_statistics = "inputfile:url=" + dtr->get_source()->str() + ',';
      if (!job_input_read_file(jobid, config, files)) {
        logger.msg(Arc::WARNING,"%s: Failed to read list of input files", jobid);
      } else {
        // go through list and take out this file
        for (std::list<FileData>::iterator i = files.begin(); i != files.end();) {
          // compare 'standard' URLs
          Arc::URL file_lfn(i->lfn);
          Arc::URL dtr_lfn(dtr->get_source()->str());
          if (file_lfn.str() == dtr_lfn.str()) {
            struct stat st;
            Arc::FileStat(job.SessionDir() + i->pfn, &st, job_uid, job_gid, true);
            dtr_transfer_statistics += "size=" + Arc::tostring(st.st_size) + ',';
            i = files.erase(i);
            break;
          } else {
            ++i;
          }
        }
        // write back .input file
        if (!job_input_write_file(job, config, files)) {
          logger.msg(Arc::WARNING, "%s: Failed to write list of input files", jobid);
        }
      }
      dtr_transfer_statistics += "starttime=" + dtr->get_creation_time().str(Arc::UTCTime) + ',';
      dtr_transfer_statistics += "endtime=" + Arc::Time().str(Arc::UTCTime) + ',';
      if (dtr->get_cache_state() == DataStaging::CACHE_ALREADY_PRESENT)
        dtr_transfer_statistics += "fromcache=yes";
      else
        dtr_transfer_statistics += "fromcache=no"; 
    }
    else {
      // transfer between two remote endpoints, shouldn't happen...
      logger.msg(Arc::WARNING, "%s: Received DTR with two remote endpoints!");
    }
  }

  // get DTRs for this job id
  lock.lock();
  std::pair<std::multimap<std::string, std::string>::iterator,
            std::multimap<std::string, std::string>::iterator> dtr_iterator = active_dtrs.equal_range(jobid);

  if (dtr_iterator.first == dtr_iterator.second) {
    lock.unlock();
    logger.msg(Arc::WARNING, "No active job id %s", jobid);
    return true;
  }
  
  // Print transfer statistics
  std::string fname = config.ControlDir() + "/job." + job.get_id() + ".statistics";
  std::ofstream f(fname.c_str(),std::ios::out | std::ios::app);
  if(f.is_open() ) {
    f << dtr_transfer_statistics << std::endl;  
  }
  f.close();
  
  // remove this DTR from list
  for (std::multimap<std::string, std::string>::iterator i = dtr_iterator.first; i != dtr_iterator.second; ++i) {
    if (i->second == dtr->get_id()) {
      active_dtrs.erase(i);
      break;
    }
  }

  // check if any DTRs left from this job, if so return
  if (active_dtrs.find(jobid) != active_dtrs.end()) {
    lock.unlock();
    return true;
  }

  // no DTRs left, clean up session dir if upload or failed download
  bool finished_with_error = (finished_jobs.find(jobid) != finished_jobs.end() && !finished_jobs[jobid].empty());
  lock.unlock();
  if (dtr->get_source()->Local()) {
    // list of files to keep in session dir
    std::list<FileData> files;
    if (!job_output_read_file(jobid, config, files))
      logger.msg(Arc::WARNING, "%s: Failed to read list of output files, can't clean up session dir", jobid);
    else {
      if (finished_with_error) {
        // if error with uploads, don't remove dynamic output files so that resume will work
        for (std::list<FileData>::iterator i = files.begin(); i != files.end(); ++i) {
          if (i->pfn.size() > 1 && i->pfn[1] == '@') {
            std::string dynamic_output(session_dir+'/'+i->pfn.substr(2));
            FileData fd(std::string(i->pfn.erase(1,1)), "");
            files.push_back(fd);
            // also add files left inside dynamic output file
            std::list<FileData> dynamic_files;
            if (!job_Xput_read_file(dynamic_output, dynamic_files, job_uid, job_gid)) {
              logger.msg(Arc::WARNING, "%s: Failed to read dynamic output files in %s", jobid, dynamic_output);
            } else {
              for (std::list<FileData>::iterator dynamic_file = dynamic_files.begin();
                   dynamic_file != dynamic_files.end(); ++dynamic_file) {
                FileData f(dynamic_file->pfn, "");
                files.push_back(f);
              }
            }
          }
        }
      }
      if (delete_all_files(job.SessionDir(), files, true, job_uid, job_gid) == 2)
        logger.msg(Arc::WARNING, "%s: Failed to clean up session dir", jobid);
    }
    // clean up cache joblinks
    CleanCacheJobLinks(config, job);
  }
  else if (finished_with_error) {
    // clean all files still in input list which could be half-downloaded
    std::list<FileData> files;
    if (!job_input_read_file(jobid, config, files))
      logger.msg(Arc::WARNING, "%s: Failed to read list of input files, can't clean up session dir", jobid);
    else if (delete_all_files(job.SessionDir(), files, false, job_uid, job_gid) == 2)
      logger.msg(Arc::WARNING, "%s: Failed to clean up session dir", jobid);
  }
  // add to finished jobs (without overwriting existing error)
  lock.lock();
  finished_jobs[jobid] += "";

  // log summary to DTR log and A-REX log
  if (finished_jobs[jobid].empty())
    dtr->get_logger()->msg(Arc::INFO, "%s: All %s %s successfully", jobid,
                          dtr->get_source()->Local() ? "uploads":"downloads",
                          (dtr->get_status() == DataStaging::DTRStatus::CANCELLED) ? "cancelled":"finished");
  else
    dtr->get_logger()->msg(Arc::INFO, "%s: Some %s failed", jobid,
                          dtr->get_source()->Local() ? "uploads":"downloads");
  lock.unlock();

  // wake up GM thread
  if (kicker_func)
    (*kicker_func)(kicker_arg);

  return true;
}


bool DTRGenerator::processReceivedJob(const GMJob& job) {

  JobId jobid(job.get_id());
  logger.msg(Arc::VERBOSE, "%s: Received data staging request to %s files", jobid,
             (job.get_state() == JOB_STATE_PREPARING ? "download" : "upload"));

  uid_t job_uid = config.StrictSession() ? job.get_user().get_uid() : 0;
  uid_t job_gid = config.StrictSession() ? job.get_user().get_gid() : 0;
  
  // Create a file for the transfer statistics and fix its permissions
  std::string fname = config.ControlDir() + "/job." + jobid + ".statistics";
  std::ofstream f(fname.c_str(),std::ios::out | std::ios::app);
  f.close();
  fix_file_permissions(fname);

  // read in input/output files
  std::list<FileData> files;
  bool replication = false;

  // output files need to be read whether PREPARING or FINISHING
  std::list<FileData> output_files;
  if (!job_output_read_file(jobid, config, output_files)) {
    logger.msg(Arc::ERROR, "%s: Failed to read list of output files", jobid);
    lock.lock();
    finished_jobs[jobid] = std::string("Failed to read list of output files");
    lock.unlock();
    if (kicker_func) (*kicker_func)(kicker_arg);
    return false;
  }

  if (job.get_state() == JOB_STATE_PREPARING) {
    if (!job_input_read_file(jobid, config, files)) {
      logger.msg(Arc::ERROR, "%s: Failed to read list of input files", jobid);
      lock.lock();
      finished_jobs[jobid] = std::string("Failed to read list of input files");
      lock.unlock();
      if (kicker_func) (*kicker_func)(kicker_arg);
      return false;
    }
    // check for duplicates (see bug 1285)
    for (std::list<FileData>::iterator i = files.begin(); i != files.end(); i++) {
      for (std::list<FileData>::iterator j = files.begin(); j != files.end(); j++) {
        if (i != j && j->pfn == i->pfn) {
          logger.msg(Arc::ERROR, "%s: Duplicate file in list of input files: %s", jobid, i->pfn);
          lock.lock();
          finished_jobs[jobid] = std::string("Duplicate file in list of input files: " + i->pfn);
          lock.unlock();
          if (kicker_func) (*kicker_func)(kicker_arg);
          return false;
        }
      }
    }
    // check if any input files are also output files (bug 1387 and 2793)
    for (std::list<FileData>::iterator j = output_files.begin(); j != output_files.end(); j++) {
      for (std::list<FileData>::iterator i = files.begin(); i != files.end(); i++) {
        if (i->pfn == j->pfn && i->lfn.find(':') != std::string::npos) {
          Arc::URL u(i->lfn);
          std::string opt = u.Option("cache");
          // don't add copy option if exists or current option is "no" or "renew"
          if (opt.empty() || !(opt == "no" || opt == "renew" || opt == "copy")) {
            u.AddOption("cache", "copy", true);
            i->lfn = u.fullstr();
          }
        }
      }
    }
    // pre-clean session dir before downloading
    if (delete_all_files(job.SessionDir(), files, false, job_uid, job_gid) == 2) {
      logger.msg(Arc::ERROR, "%s: Failed to clean up session dir", jobid);
      lock.lock();
      finished_jobs[jobid] = std::string("Failed to clean up session dir before downloading inputs");
      lock.unlock();
      if (kicker_func) (*kicker_func)(kicker_arg);
      return false;
    }
  }
  else if (job.get_state() == JOB_STATE_FINISHING) {
    files = output_files;
    std::list<FileData>::iterator it;
    // add any output files dynamically added by the user during the job
    for (it = files.begin(); it != files.end() ; ++it) {
      if (it->pfn.find("@") == 1) { // GM puts a slash on the front of the local file
        std::string outputfilelist = job.SessionDir() + std::string("/") + it->pfn.substr(2);
        logger.msg(Arc::INFO, "%s: Reading output files from user generated list in %s", jobid, outputfilelist);
        if (!job_Xput_read_file(outputfilelist, files, job_uid, job_gid)) {
          logger.msg(Arc::ERROR, "%s: Error reading user generated output file list in %s", jobid, outputfilelist);
          lock.lock();
          finished_jobs[jobid] = std::string("Error reading user generated output file list");
          lock.unlock();
          if (kicker_func) (*kicker_func)(kicker_arg);
          return false;
        }
        it->pfn.erase(1, 1);
      }
    }
    // check if any files share the same LFN, if so allow overwriting existing LFN
    for (it = files.begin(); it != files.end(); it++) {
      bool done = false;
      for (std::list<FileData>::iterator it2 = files.begin(); it2 != files.end(); it2++) {
        if (it != it2 && !it->lfn.empty() && !it2->lfn.empty()) {
          // error if lfns (including locations) are identical
          if (it->lfn == it2->lfn) {
            logger.msg(Arc::ERROR, "%s: Two identical output destinations: %s", jobid, it->lfn);
            lock.lock();
            finished_jobs[jobid] = std::string("Two identical output destinations: " + it->lfn);
            lock.unlock();
            if (kicker_func) (*kicker_func)(kicker_arg);
            return false;
          }
          Arc::URL u_it(it->lfn);
          Arc::URL u_it2(it2->lfn);
          if (u_it == u_it2) {
            // error if pfns are different
            if (it->pfn != it2->pfn) {
              logger.msg(Arc::ERROR, "%s: Cannot upload two different files %s and %s to same LFN: %s", jobid, it->pfn, it2->pfn, it->lfn);
              lock.lock();
              finished_jobs[jobid] = std::string("Cannot upload two different files to same LFN: " + it->lfn);
              lock.unlock();
              if (kicker_func) (*kicker_func)(kicker_arg);
              return false;
            }
            replication = true;
            done = true;
            break;
          }
        }
      }
      if (done)
        break;
    }
    // pre-clean session dir before uploading
    if (delete_all_files(job.SessionDir(), files, true, job_uid, job_gid) == 2) {
      logger.msg(Arc::ERROR, "%s: Failed to clean up session dir", jobid);
      lock.lock();
      finished_jobs[jobid] = std::string("Failed to clean up session dir before uploading outputs");
      lock.unlock();
      if (kicker_func) (*kicker_func)(kicker_arg);
      return false;
    }
  }
  else {
    // bad state
    logger.msg(Arc::ERROR, "%s: Received job in a bad state: %s", jobid, job.get_state_name());
    lock.lock();
    finished_jobs[jobid] = std::string("Logic error: DTR Generator received job in a bad state");
    lock.unlock();
    if (kicker_func) (*kicker_func)(kicker_arg);
    return false;
  }
  Arc::initializeCredentialsType cred_type(Arc::initializeCredentialsType::SkipCredentials);
  Arc::UserConfig usercfg(cred_type);
  usercfg.UtilsDirPath(config.ControlDir());
  usercfg.CACertificatesDirectory(config.CertDir());
  if (config.StrictSession()) usercfg.SetUser(job.get_user());
  // TODO: chelonia bartenders

  // create job.id.errors file with correct permissions to add to Logger
  job_errors_mark_put(job, config);

  if (files.empty()) {
    // if job is FINISHING then clean up cache joblinks
    if (job.get_state() == JOB_STATE_FINISHING) CleanCacheJobLinks(config, job);
    // nothing else to do so wake up GM thread and return
    lock.lock();
    finished_jobs[jobid] = "";
    lock.unlock();
    if (kicker_func) (*kicker_func)(kicker_arg);
    return true;
  }

  // flag to say whether at least one file needs to be staged
  bool staging = false;

  for (std::list<FileData>::iterator i = files.begin(); i != files.end(); ++i) {
    if (i->lfn.find(":") == std::string::npos)
      continue; // user down/uploadable file

    staging = true;
    std::string source;
    std::string original_source;
    std::string destination;
    if (job.get_state() == JOB_STATE_PREPARING) {
      // If ACIX should be used, use it as source and add original URL as
      // location after DTR is created
      if (!config.ACIXEndpoint().empty()) {
        // Encode the original url so it is not parsed as part of the acix url
        original_source = i->lfn;
        Arc::URL acix_source(config.ACIXEndpoint() + "?url=" + Arc::uri_encode(original_source, false));
        Arc::URL original_url(original_source);
        // Add URL options to ACIX URL
        for (std::map<std::string, std::string>::const_iterator opt = original_url.Options().begin();
             opt != original_url.Options().end(); ++opt) {
          acix_source.AddOption(opt->first, opt->second);
        }
        source = acix_source.fullstr();
      }
      else {
        source = i->lfn;
      }
      destination = "file:" + job.SessionDir() + i->pfn;
    }
    else {
      source = "file:" + job.SessionDir() + i->pfn;
      destination = i->lfn;
    }
    // Check if this file was recovered from a crash, if so add overwrite option
    for (std::list<std::string>::iterator file = recovered_files.begin(); file != recovered_files.end();) {
      if (*file == destination) {
        logger.msg(Arc::WARNING, "%s: Destination file %s was possibly left unfinished"
            " from previous A-REX run, will overwrite", jobid, destination);
        Arc::URL u(destination);
        if (u) {
          u.AddOption("overwrite=yes", true);
          destination = u.fullstr();
        }
        file = recovered_files.erase(file);
      } else {
        ++file;
      }
    }

    std::string proxy_cred;
    if(!i->cred.empty()) {
      usercfg.ProxyPath(i->cred);
      if (Arc::FileRead(i->cred, proxy_cred)) usercfg.CredentialString(proxy_cred);
    } else {
      usercfg.ProxyPath(job_proxy_filename(jobid, config));
      if (Arc::FileRead(job_proxy_filename(jobid, config), proxy_cred)) usercfg.CredentialString(proxy_cred);
    }
    // logger for these DTRs. LogDestinations should be deleted when DTR is received back
    DataStaging::DTRLogger dtr_log(new Arc::Logger(Arc::Logger::getRootLogger(), "DataStaging.DTR"));
    Arc::LogFile * dest = new Arc::LogFile(job_errors_filename(jobid, config));
    dest->setReopen(true);
    dest->setFormat(Arc::MediumFormat);
    dtr_log->addDestination(*dest);

    // create DTR and send to Scheduler
    DataStaging::DTR_ptr dtr(new DataStaging::DTR(source, destination, usercfg, jobid, job.get_user().get_uid(), dtr_log));
    // set retry count (tmp errors only)
    dtr->set_tries_left(staging_conf.max_retries);
    // allow the same file to be uploaded to multiple locations with same LFN
    dtr->set_force_registration(replication);
    // set sub-share for download or upload
    dtr->set_sub_share((job.get_state() == JOB_STATE_PREPARING) ? "download" : "upload");
    // set priority as given in job description
    if (job.get_local())
      dtr->set_priority(job.get_local()->priority);
    // set whether to use A-REX host certificate for remote delivery services
    dtr->host_cert_for_remote_delivery(staging_conf.use_host_cert_for_remote_delivery);
    // set real location if ACIX is used
    if (!original_source.empty()) {
      dtr->get_source()->AddLocation(Arc::URL(original_source), Arc::URL(original_source).ConnectionURL());
    }

    DataStaging::DTRCacheParameters cache_parameters;
    CacheConfig cache_params(config.CacheParams());
    // Substitute cache paths
    cache_params.substitute(config, job.get_user());
    cache_parameters.cache_dirs = cache_params.getCacheDirs();
    cache_parameters.remote_cache_dirs = cache_params.getRemoteCacheDirs();
    dtr->set_cache_parameters(cache_parameters);
    dtr->registerCallback(this,DataStaging::GENERATOR);
    dtr->registerCallback(scheduler, DataStaging::SCHEDULER);
    // callbacks for info
    dtr->registerCallback(&info, DataStaging::SCHEDULER);
    lock.lock();
    active_dtrs.insert(std::pair<std::string, std::string>(jobid, dtr->get_id()));
    lock.unlock();
    // send to Scheduler
    DataStaging::DTR::push(dtr, DataStaging::SCHEDULER);

    // update .local with transfer share
    JobLocalDescription *job_desc = new JobLocalDescription;
    if (!job_local_read_file(jobid, config, *job_desc)) {
      logger.msg(Arc::ERROR, "%s: Failed reading local information", jobid);
      delete job_desc;
      continue;
    }
    job_desc->transfershare = dtr->get_transfer_share();
    if (!job_local_write_file(job, config, *job_desc)) {
      logger.msg(Arc::ERROR, "%s: Failed writing local information", jobid);
    }
    delete job_desc;
  }
  if (!staging) { // nothing needed staged so mark as finished
    // if job is FINISHING then clean up cache joblinks
    if (job.get_state() == JOB_STATE_FINISHING) CleanCacheJobLinks(config, job);
    lock.lock();
    finished_jobs[jobid] = "";
    lock.unlock();
  }
  return true;
}

bool DTRGenerator::processCancelledJob(const std::string& jobid) {

  // cancel DTRs in Scheduler
  logger.msg(Arc::INFO, "%s: Cancelling active DTRs", jobid);
  scheduler->cancelDTRs(jobid);
  return true;
}

int DTRGenerator::checkUploadedFiles(GMJob& job) {

  JobId jobid(job.get_id());
  uid_t job_uid = config.StrictSession() ? job.get_user().get_uid() : 0;
  uid_t job_gid = config.StrictSession() ? job.get_user().get_gid() : 0;

  std::string session_dir;
  if (job.get_local() && !job.get_local()->sessiondir.empty()) session_dir = job.get_local()->sessiondir;
  else session_dir = config.SessionRoot(jobid) + '/' + jobid;
  // get input files list
  std::list<std::string> uploaded_files;
  std::list<std::string>* uploaded_files_ = NULL;
  std::list<FileData> input_files;
  std::list<FileData> input_files_ = input_files;
  if (!job_input_read_file(jobid, config, input_files)) {
    job.AddFailure("Error reading list of input files");
    logger.msg(Arc::ERROR, "%s: Can't read list of input files", jobid);
    return 1;
  }
  if (job_input_status_read_file(jobid, config, uploaded_files)) {
    uploaded_files_ = &uploaded_files;
  }
  int res = 0;

  // loop through each file and check
  for (FileData::iterator i = input_files.begin(); i != input_files.end();) {
    // all remote files should have been downloaded by this point
    if (i->lfn.find(":") != std::string::npos) {
      ++i;
      continue;
    }
    logger.msg(Arc::VERBOSE, "%s: Checking user uploadable file: %s", jobid, i->pfn);
    std::string error;
    int err = user_file_exists(*i, session_dir, jobid, error, job_uid, job_gid, uploaded_files_);

    if (err == 0) { // file is uploaded
      logger.msg(Arc::VERBOSE, "%s: User has uploaded file %s", jobid, i->pfn);
      // remove from input list
      i = input_files.erase(i);
      input_files_.clear();
      for (FileData::iterator it = input_files.begin(); it != input_files.end(); ++it)
        input_files_.push_back(*it);
      if (!job_input_write_file(job, config, input_files_)) {
        logger.msg(Arc::WARNING, "%s: Failed writing changed input file.", jobid);
      }
    }
    else if (err == 1) { // critical failure
      logger.msg(Arc::ERROR, "%s: Critical error for uploadable file %s", jobid, i->pfn);
      job.AddFailure("User file: "+i->pfn+" - "+error);
      res = 1;
      break;
    }
    else { // still waiting
      res = 2;
      ++i;
    }
  }
  // check for timeout
  if (res == 2 && ((time(NULL) - job.GetStartTime()) > 600)) { // hard-coded timeout
    for (FileData::iterator i = input_files.begin(); i != input_files.end(); ++i) {
      if (i->lfn.find(":") == std::string::npos) {
        job.AddFailure("User file: "+i->pfn+" - Timeout waiting");
      }
    }
    logger.msg(Arc::ERROR, "%s: Uploadable files timed out", jobid);
    res = 1;
  }
  return res;
}

bool match_list(const std::list<std::string>& slist, const std::string& str) {
  for(std::list<std::string>::const_iterator s = slist.begin();
               s != slist.end(); ++s) {
    if(*s == str) return true;
  }
  return false;
}



int DTRGenerator::user_file_exists(FileData &dt,
                                   const std::string& session_dir,
                                   const std::string& jobid,
                                   std::string& error,
                                   uid_t uid, gid_t gid,
                                   const std::list<std::string>* uploaded_files) {
  struct stat st;
  std::string file_info(dt.lfn);
  if (file_info == "*.*") return 0; // do not wait for this file

  std::string fname = session_dir + '/' + dt.pfn;
  // check if file exists at all
  if (!Arc::FileStat(fname.c_str(), &st, uid, gid, false)) return 2;

  // if no size/checksum was supplied, return success
  if (file_info.empty()) {
    // but check status first if avaialble
    if (uploaded_files) {
      if (!match_list(*uploaded_files, dt.pfn)) return 2;
    }
    return 0;
  }

  if (S_ISDIR(st.st_mode)) {
    error = "Expected file. Directory found.";
    return 1;
  }
  if (!S_ISREG(st.st_mode)) {
    error = "Expected ordinary file. Special object found.";
    return 1;
  }

  long long int fsize;
  long long int fsum;
  bool have_size = false;
  bool have_checksum = false;

  // parse format [size][.checksum]
  if (file_info[0] == '.') { // checksum only
    if (!Arc::stringto(file_info.substr(1), fsum)) {
      logger.msg(Arc::ERROR, "%s: Can't convert checksum %s to int for %s", jobid, file_info.substr(1), dt.pfn);
      error = "Invalid checksum information";
      return 1;
    }
    have_checksum = true;
  } else if (file_info.find('.') == std::string::npos) { // size only
    if (!Arc::stringto(file_info, fsize)) {
      logger.msg(Arc::ERROR, "%s: Can't convert filesize %s to int for %s", jobid, file_info, dt.pfn);
      error = "Invalid file size information";
      return 1;
    }
    have_size = true;
  } else { // size and checksum
    std::vector<std::string> file_attrs;
    Arc::tokenize(dt.lfn, file_attrs, ".");
    if (file_attrs.size() != 2) {
      logger.msg(Arc::ERROR, "%s: Invalid size/checksum information (%s) for %s", jobid, file_info, dt.pfn);
      error = "Invalid size/checksum information";
      return 1;
    }
    if (!Arc::stringto(file_attrs[0], fsize)) {
      logger.msg(Arc::ERROR, "%s: Can't convert filesize %s to int for %s", jobid, file_attrs[0], dt.pfn);
      error = "Invalid file size information";
      return 1;
    }
    if (!Arc::stringto(file_attrs[1], fsum)) {
      logger.msg(Arc::ERROR, "%s: Can't convert checksum %s to int for %s", jobid, file_attrs[1], dt.pfn);
      error = "Invalid checksum information";
      return 1;
    }
    have_size = true;
    have_checksum = true;
  }

  // now check if proper size
  if (have_size) {
    if (st.st_size < fsize) return 2;
    if (st.st_size > fsize) {
      logger.msg(Arc::ERROR, "%s: Invalid file: %s is too big.", jobid, dt.pfn);
      error = "Delivered file is bigger than specified.";
      return 1;
    }
  }

  if (uploaded_files) {
    if (!match_list(*uploaded_files, dt.pfn)) return 2;
  } else if (have_checksum) { // calculate checksum (if no better way)
    int h = -1;
    Arc::FileAccess* fa = NULL;

    if ((uid && uid != getuid()) || (gid && gid != getgid())) {
      fa = new Arc::FileAccess();
      if (!fa->fa_setuid(uid, gid)) {
        delete fa;
        logger.msg(Arc::ERROR, "%s: Failed to switch user ID to %d/%d to read file %s", jobid, (unsigned int)uid, (unsigned int)gid, dt.pfn);
        error = "Could not switch user id to read file";
        return 1;
      }
      if(!fa->fa_open(fname, O_RDONLY, 0)) {
        delete fa;
        logger.msg(Arc::ERROR, "%s: Failed to open file %s for reading", jobid, dt.pfn);
        error = "Failed to open file for reading";
        return 1;
      }
    }
    else {
      h = ::open(fname.c_str(), O_RDONLY);
      if (h == -1) { // if we can't read that file job won't too
        logger.msg(Arc::ERROR, "%s: Error accessing file %s", jobid, dt.pfn);
        error = "Delivered file is unreadable.";
        return 1;
      }
    }
    Arc::CRC32Sum crc;
    char buffer[1024];
    ssize_t l;
    for(;;) {
      if (fa) l = fa->fa_read(buffer, 1024);
      else l = read(h, buffer, 1024);
      if (l == -1) {
        logger.msg(Arc::ERROR, "%s: Error reading file %s", jobid, dt.pfn);
        error = "Could not read file to compute checksum.";
        delete fa;
        return 1;
      }
      if (l == 0) break;
      crc.add(buffer, l);
    }
    close(h);
    if (fa) fa->fa_close();
    delete fa;
    crc.end();

    if (fsum != crc.crc()) {
      if (have_size) { // size was checked and is ok
        logger.msg(Arc::ERROR, "%s: File %s has wrong checksum: %llu. Expected %lli", jobid, dt.pfn, crc.crc(), fsum);
        error = "Delivered file has wrong checksum.";
        return 1;
      }
      return 2; // not uploaded yet
    }
    logger.msg(Arc::VERBOSE, "%s: Checksum %llu verified for %s", jobid, crc.crc(), dt.pfn);
  }
  return 0; // all checks passed - file is ok
}

void DTRGenerator::readDTRState(const std::string& dtr_log) {

  std::list<std::string> lines;
  // file may not exist if this is the first use of DTR
  if (!Arc::FileRead(dtr_log, lines)) return;

  if (!lines.empty()) {
    logger.msg(Arc::WARNING, "Found unfinished DTR transfers. It is possible the "
        "previous A-REX process did not shut down normally");
  }
  for (std::list<std::string>::iterator line = lines.begin(); line != lines.end(); ++line) {
    std::vector<std::string> fields;
    Arc::tokenize(*line, fields);
    if ((fields.size() == 5 || fields.size() == 6) && (fields.at(1) == "TRANSFERRING" || fields.at(1) == "TRANSFER")) {
      logger.msg(Arc::VERBOSE, "Found DTR %s for file %s left in transferring state from previous run",
                 fields.at(0), fields.at(4));
      recovered_files.push_back(fields.at(4));
    }
  }
}

void DTRGenerator::CleanCacheJobLinks(const GMConfig& config, const GMJob& job) const {

  CacheConfig cache_config(config.CacheParams());
  cache_config.substitute(config, job.get_user());
  // there is no uid switch during Release so uid/gid is not so important
  Arc::FileCache cache(cache_config.getCacheDirs(),
                       cache_config.getRemoteCacheDirs(),
                       cache_config.getDrainingCacheDirs(),
                       job.get_id(), job.get_user().get_uid(), job.get_user().get_gid());
  cache.Release();
}

} // namespace ARex
