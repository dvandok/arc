#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <algorithm>

#include <glibmm/fileutils.h>

#include <arc/client/ACC.h>

namespace Arc {

  ACC::ACC(Config *cfg, const std::string& flavour)
    : flavour(flavour) {
    proxyPath = (std::string)(*cfg)["ProxyPath"];
    certificatePath = (std::string)(*cfg)["CertificatePath"];
    keyPath = (std::string)(*cfg)["KeyPath"];
    caCertificatesDir = (std::string)(*cfg)["CACertificatesDir"];
  }

  ACC::~ACC() {}

  const std::string& ACC::Flavour() {
    return flavour;
  }

  XMLNode ACCConfig::MakeConfig(XMLNode cfg) const {
    XMLNode mm = BaseConfig::MakeConfig(cfg);
    std::list<std::string> accs;
    for (std::list<std::string>::const_iterator path = plugin_paths.begin();
	 path != plugin_paths.end(); path++) {
      try {
	Glib::Dir dir(*path);
	for (Glib::DirIterator file = dir.begin(); file != dir.end(); file++)
	  if ((*file).substr(0, 6) == "libacc") {
	    std::string name = (*file).substr(6, (*file).find('.') - 6);
	    if (std::find(accs.begin(), accs.end(), name) == accs.end()) {
	      accs.push_back(name);
	      cfg.NewChild("Plugins").NewChild("Name") = "acc" + name;
	    }
	  }
      }
      catch (Glib::FileError) {}
    }
    return mm;
  }

} // namespace Arc
