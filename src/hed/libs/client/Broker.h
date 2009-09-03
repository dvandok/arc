// -*- indent-tabs-mode: nil -*-

#ifndef __ARC_BROKER_H__
#define __ARC_BROKER_H__

#include <list>
#include <string>

#include <arc/loader/Loader.h>
#include <arc/loader/Plugin.h>

namespace Arc {

  class Config;
  class ExecutionTarget;
  class JobDescription;
  class Logger;
  class UserConfig;

  class Broker
    : public Plugin {

  public:
    /// Returns next target from the list of ExecutionTarget objects
    /** When first called this method will sort its list of ExecutionTarget
        objects, which have been filled by the PreFilterTargets method, and
        then the first target in the list will be returned.

        If this is not the first call then the next target in the list is
        simply returned.

        If there are no targets in the list or the end of the target list have
        been reached the NULL pointer is returned.

        \return The pointer to the next ExecutionTarget in the list is returned.
     */
    const ExecutionTarget* GetBestTarget();
    /// ExecutionTarget filtering, view-point: enought memory, diskspace, CPUs, etc.
    /** The "bad" targets will be ignored and only the good targets will be added to
        to the list of ExecutionTarget objects which be used for brokering.
        \param targets A list of ExecutionTarget objects to be considered for
               addition to the Broker.
        \param jd JobDescription object of the actual job.
     */
    void PreFilterTargets(std::list<ExecutionTarget>& targets,
                          const JobDescription& job);

    /// Register a job submission to the current target
    void RegisterJobsubmission();

  protected:
    Broker(const Config& cfg, const UserConfig& usercfg);
  public:
    virtual ~Broker();
  protected:
    /// Custom Brokers should implement this method
    /** The task is to sort the PossibleTargets list by "custom"
        way, for example: FastestQueueBroker, ExecutionTarget which has
        the shortest queue lenght will be at the begining of the PossibleTargets list
     */
    virtual void SortTargets() = 0;

    const UserConfig& usercfg;

    /// This content the Prefilteres ExecutionTargets
    /** If an Execution Tartget has enought memory, CPU, diskspace, etc. for the
        actual job requirement than it will be added to the PossibleTargets list
     */
    std::list<ExecutionTarget*> PossibleTargets;
    /// It is true if "custom" sorting is done
    bool TargetSortingDone;
    const JobDescription *job;

    static Logger logger;

  private:
    /// This is a pointer for the actual ExecutionTarget in the
    /// PossibleTargets list
    std::list<ExecutionTarget*>::iterator current;
  };

  //! Class responsible for loading Broker plugins
  /// The Broker objects returned by a BrokerLoader
  /// must not be used after the BrokerLoader goes out of scope.
  class BrokerLoader
    : public Loader {

  public:
    //! Constructor
    /// Creates a new BrokerLoader.
    BrokerLoader();

    //! Destructor
    /// Calling the destructor destroys all Brokers loaded
    /// by the BrokerLoader instance.
    ~BrokerLoader();

    //! Load a new Broker
    /// \param name    The name of the Broker to load.
    /// \param cfg     The Config object for the new Broker.
    /// \param usercfg The UserConfig object for the new Broker.
    /// \returns       A pointer to the new Broker (NULL on error).
    Broker* load(const std::string& name,
                 const Config& cfg, const UserConfig& usercfg);

    //! Retrieve the list of loaded Brokers.
    /// \returns A reference to the list of Brokers.
    const std::list<Broker*>& GetBrokers() const {
      return brokers;
    }

  private:
    std::list<Broker*> brokers;
  };

  class BrokerPluginArgument
    : public PluginArgument {
  public:
    BrokerPluginArgument(const Config& cfg, const UserConfig& usercfg)
      : cfg(cfg),
        usercfg(usercfg) {}
    ~BrokerPluginArgument() {}
    operator const Config&() {
      return cfg;
    }
    operator const UserConfig&() {
      return usercfg;
    }
  private:
    const Config& cfg;
    const UserConfig& usercfg;
  };

} // namespace Arc

#endif // __ARC_BROKER_H__
