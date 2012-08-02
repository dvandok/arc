#ifndef __ARC_JOBSTATEEMIES_H__
#define __ARC_JOBSTATEEMIES_H__

#include <arc/client/JobState.h>

#include "EMIESClient.h"

namespace Arc {

  class JobStateEMIES
    : public JobState {
  public:
    JobStateEMIES(const std::string& state): JobState(state, &StateMapS, FormatSpecificState) {}
    // TODO: extremely suboptimal
    JobStateEMIES(XMLNode state): JobState(xml_to_string(state), &StateMapX, FormatSpecificState) {}
    static JobState::StateType StateMapS(const std::string& state);
    static JobState::StateType StateMapX(const std::string& state);
    static JobState::StateType StateMapInt(const EMIESJobState& st);
    
    static std::string FormatSpecificState(const std::string& state);
  private:
    std::string xml_to_string(XMLNode xml) {
      std::string s;
      xml.GetXML(s);
      return s;
    };
  };

}

#endif // __ARC_JOBSTATEEMIES_H__
