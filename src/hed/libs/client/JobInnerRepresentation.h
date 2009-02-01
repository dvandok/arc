#ifndef __ARC_JOBINNERREPRESENTATION_H__
#define __ARC_JOBINNERREPRESENTATION_H__

#include <string>
#include <map>

#include <arc/DateTime.h>
#include <arc/URL.h>
#include <arc/XMLNode.h>


namespace Arc {

struct ReferenceTimeType {
    std::string benchmark_attribute;
    std::string value_attribute;
    std::string value; // TODO: this should be number
};

struct EnvironmentType {
    std::string name_attribute;
    std::string value;
};

struct XLogueType {
    std::string Name;
    std::list<std::string> Arguments;
};

struct RunTimeEnvironmentType {
    std::string Name;
    std::list<std::string> Version;
};

struct NotificationType {
    std::list<std::string> Address;
    std::list<std::string> State;
};

struct SourceType {
    URL URI;
    int Threads;
};

struct TargetType {
    URL URI;
    int Threads;
    bool Mandatory;
    int NeededReplicas;
};

struct FileType {
    std::string Name;
    std::list<Arc::SourceType> Source;
    std::list<Arc::TargetType> Target;
    bool KeepData;
    bool IsExecutable;
    URL DataIndexingService;
    bool DownloadToCache;
};

struct DirectoryType {
    std::string Name;
    std::list<Arc::SourceType> Source;
    std::list<Arc::TargetType> Target;
    bool KeepData;
    bool IsExecutable;
    URL DataIndexingService;
    bool DownloadToCache;
};

  class JobInnerRepresentation {

  public:

    JobInnerRepresentation();
    //~JobInnerRepresentation();

    // Copy constructor
    JobInnerRepresentation(const JobInnerRepresentation&);

    // Special language wrapper constructor
    JobInnerRepresentation(const long int ptraddr);

    // Print all value to the standard output.
    void Print(bool longlist) const;
    void Reset();

    // Export the inner representation into the jobTree XMLNode.
    bool getXML( Arc::XMLNode& jobTree) const;

    // Meta information
    std::list<std::string> OptionalElement;
    std::string Author;
    Time DocumentExpiration;
    std::string Rank;
    bool FuzzyRank;

    // JobIdentification information
    std::string JobName;
    std::string Description;
    std::string JobProject;
    std::string JobType;
    std::string JobCategory;
    std::list<std::string> UserTag;

    // Application information
    std::string Executable;
    std::list<std::string> Argument;
    std::string Input;
    std::string Output;
    std::string Error;
    std::string LogDir;
    URL RemoteLogging;
    std::list<Arc::EnvironmentType> Environment;
    int LRMSReRun;
    Arc::XLogueType Prologue;
    Arc::XLogueType Epilogue;
    Period SessionLifeTime;
    Arc::XMLNode AccessControl;
    Time ProcessingStartTime;
    std::list<Arc::NotificationType> Notification;
    URL CredentialService;
    bool Join;
   

    // Resource information
    Period TotalCPUTime;
    Period IndividualCPUTime;
    Period TotalWallTime;
    Period IndividualWallTime;
    Arc::ReferenceTimeType ReferenceTime;
    bool ExclusiveExecution;
    std::string NetworkInfo;
    std::string OSFamily;
    std::string OSName;
    std::string OSVersion;
    std::string Platform;
    int IndividualPhysicalMemory;
    int IndividualVirtualMemory;
    int DiskSpace;
    int CacheDiskSpace;         //DiskSpace
    int SessionDiskSpace;       //DiskSpace
    int IndividualDiskSpace;    //DiskSpace
    URL EndPointURL;            //CandidateTarget
    std::string QueueName;      //CandidateTarget
    std::string CEType;
    int Slots;
    int NumberOfProcesses;      //Slots
    int ProcessPerHost;	        //Slots
    int ThreadPerProcesses;     //Slots
    std::string SPMDVariation;  //Slots
    std::list<Arc::RunTimeEnvironmentType> RunTimeEnvironment;
    bool Homogeneous;
    bool InBound;               //NodeAccess
    bool OutBound;              //NodeAccess


    // DataStaging information
    std::list<Arc::FileType> File;
    std::list<Arc::DirectoryType> Directory;
    URL DataIndexingService;    //Defaults
    URL StagingInBaseURI;       //Defaults
    URL StagingOutBaseURI;      //Defaults

    //other specified elements
    std::map<std::string, std::string> XRSL_elements;
    std::map<std::string, std::string> JDL_elements;
    bool cached;

  };

} // namespace Arc

#endif // __ARC_JOBINNERREPRESENTATION_H__
