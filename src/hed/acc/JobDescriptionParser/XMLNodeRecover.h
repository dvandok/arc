// -*- indent-tabs-mode: nil -*-

#ifndef __ARC_XMLNODERECOVER_H__
#define __ARC_XMLNODERECOVER_H__

#include <arc/XMLNode.h>

namespace Arc {

  class XMLNodeRecover
    : public XMLNode {
  public:
    XMLNodeRecover(const std::string& xml);
    ~XMLNodeRecover();

    bool HasErrors() const { return !errors.empty(); };
    const std::list<xmlErrorPtr>& GetErrors() const { return errors; };

    void print_error(const xmlError& error);
    static void structured_error_handler(void *userData, xmlErrorPtr error);

  private:
    XMLNodeRecover(void) {};
    XMLNodeRecover(const XMLNode&) {}
    XMLNodeRecover(const char *xml, int len = -1) {}
    XMLNodeRecover(long) {}
    XMLNodeRecover(const NS&, const char *) {}

    std::list<xmlErrorPtr> errors;
  };

} // namespace Arc

#endif // __ARC_XMLNODERECOVER_H__
