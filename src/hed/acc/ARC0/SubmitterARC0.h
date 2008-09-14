#ifndef __ARC_SUBMITTERARC0_H__
#define __ARC_SUBMITTERARC0_H__

#include <arc/client/Submitter.h>

namespace Arc {

  class ChainContext;
  class Config;

  class SubmitterARC0
    : public Submitter {

  private:
    SubmitterARC0(Config *cfg);
    ~SubmitterARC0();
    static Logger logger;

  public:
    static ACC* Instance(Config *cfg, ChainContext *cxt);
    bool Submit(JobDescription& jobdesc, XMLNode& info);
  };

} // namespace Arc

#endif // __ARC_SUBMITTERARC0_H__
