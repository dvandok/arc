// -*- indent-tabs-mode: nil -*-

#ifndef __ARC_JDLPARSER_H__
#define __ARC_JDLPARSER_H__

#include <list>
#include <string>

#include <arc/compute/JobDescriptionParserPlugin.h>

/** JDLParser
 * The JDLParser class, derived from the JobDescriptionParserPlugin class, is a job
 * description parser for the Job Description Language (JDL) specified in CREAM
 * Job Description Language Attributes Specification for the EGEE middleware
 * (EGEE-JRA1-TEC-592336) and Job Description Language Attributes Specification
 * for the gLite middleware (EGEE-JRA1-TEC-590869-JDL-Attributes-v0-8).
 */

namespace Arc {

  class JDLParser
    : public JobDescriptionParserPlugin {
  public:
    JDLParser(PluginArgument* parg);
    ~JDLParser();
    JobDescriptionParserPluginResult Parse(const std::string& source, std::list<JobDescription>& jobdescs, const std::string& language = "", const std::string& dialect = "") const;
    JobDescriptionParserPluginResult UnParse(const JobDescription& job, std::string& product, const std::string& language, const std::string& dialect = "") const;

    static Plugin* Instance(PluginArgument *arg);

  private:
    bool splitJDL(const std::string& original_string,
                  std::list<std::string>& lines) const;
    bool handleJDLattribute(const std::string& attributeName,
                            const std::string& attributeValue,
                            JobDescription& job) const;
    static std::string simpleJDLvalue(const std::string& attributeValue);
    static std::list<std::string> listJDLvalue(const std::string& attributeValue,
                                               std::pair<char, char> bracket = std::make_pair('{', '}'),
                                               char lineEnd = ',');
    std::string generateOutputList(const std::string& attribute,
                                   const std::list<std::string>& list,
                                   std::pair<char, char> bracket = std::make_pair('{', '}'),
                                   char lineEnd = ',') const;
    static bool ParseInputSandboxAttribute(JobDescription&);
  };

} // namespace Arc

#endif // __ARC_JDLPARSER_H__
