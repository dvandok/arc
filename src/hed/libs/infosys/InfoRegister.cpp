#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>

#include <arc/Thread.h>
#include <arc/URL.h>
#include <arc/message/PayloadSOAP.h>
#include <arc/client/ClientInterface.h>
#include <arc/StringConv.h>
#include "InfoRegister.h"
#ifdef WIN32
#include <arc/win32.h>
#endif

static Arc::Logger logger_(Arc::Logger::rootLogger, "InfoSys");

namespace Arc {

static void reg_thread(void *data) {
    InfoRegistrar *self = (InfoRegistrar *)data;
    { // Very important!!!: Delete this block imediately!!!
        // Sleep and exit if interrupted by request to exit
        unsigned int sleep_time = 15; //seconds
        logger_.msg(DEBUG, "InfoRegistrar thread waiting %d seconds for the all Registers elements creation.", sleep_time);
        sleep(sleep_time);
    }
    self->registration();
}

// -------------------------------------------------------------------

InfoRegister::InfoRegister(XMLNode &cfg, Service *service):reg_period_(0),service_(service) {
    ns_["isis"] = ISIS_NAMESPACE;
    ns_["glue2"] = GLUE2_D42_NAMESPACE;
    ns_["register"] = REGISTRATION_NAMESPACE;

    // parse config
    std::string s_reg_period = (std::string)cfg["InfoRegister"].Attribute("period");
    if (!s_reg_period.empty()) {
        Period p(s_reg_period); 
        reg_period_ = p.GetPeriod();
    } else {
        reg_period_ = -1;
    }

    //DEBUG//
    std::string configuration_string;
    XMLNode temp;
    cfg.New(temp);
    temp.GetDoc(configuration_string, true);
    logger_.msg(DEBUG, "InfoRegister created with config:\n%s", configuration_string);

    // Add service to registration list. Optionally only for 
    // registration through specific registrants.
    std::list<std::string> ids;
    for(XMLNode r = cfg["InfoRegister"]["Registrar"];(bool)r;++r) {
      std::string id = r;
      if(!id.empty()) ids.push_back(id);
    };
    InfoRegisterContainer::Instance().addService(this,ids,cfg);
}

InfoRegister::~InfoRegister(void) {
    // This element is supposed to be destroyed with service.
    // Hence service should not be restered anymore.
    // TODO: initiate un-register of service
    InfoRegisterContainer::Instance().removeService(this);
}
    
// -------------------------------------------------------------------

InfoRegisterContainer* InfoRegisterContainer::instance_ = NULL;

InfoRegisterContainer& InfoRegisterContainer::Instance(void) {
  if(!instance_) instance_ = new InfoRegisterContainer;
  return *instance_;
}

InfoRegisterContainer::InfoRegisterContainer(void):regr_done_(false) {
}

InfoRegisterContainer::~InfoRegisterContainer(void) {
    Glib::Mutex::Lock lock(lock_);
    for(std::list<InfoRegistrar*>::iterator r = regr_.begin();
                              r != regr_.end();++r) {
        delete (*r);
    };
}

void InfoRegisterContainer::addRegistrars(XMLNode doc) {
    Glib::Mutex::Lock lock(lock_);
    for(XMLNode node = doc["InfoRegistrar"];(bool)node;++node) {
        InfoRegistrar* r = new InfoRegistrar(node);
        if(!r) continue;
        if(!(*r)) { delete r; continue; };
        regr_.push_back(r);
    };
    // Start registration threads
    for(std::list<InfoRegistrar*>::iterator r = regr_.begin();
                              r != regr_.end();++r) {
       CreateThreadFunction(&reg_thread,*r);
    };
    regr_done_=true;
}

void InfoRegisterContainer::addService(InfoRegister* reg,const std::list<std::string>& ids,XMLNode cfg) {
    // Add element to list
    //regs_.push_back(reg);
    // If not yet done create registrars
    if(!regr_done_) {
        if((bool)cfg) {
            addRegistrars(cfg.GetRoot());
        } else {
            // Bad situation
        };
    };

    // Add to registrars
    Glib::Mutex::Lock lock(lock_);
    for(std::list<InfoRegistrar*>::iterator r = regr_.begin();
                              r != regr_.end();++r) {
        if(ids.size() > 0) {
            for(std::list<std::string>::const_iterator i = ids.begin();
                                            i != ids.end();++i) {
                if((*i) == (*r)->id()) (*r)->addService(reg);
            };
        } else {
            (*r)->addService(reg);
        };
    };
}

void InfoRegisterContainer::removeService(InfoRegister* reg) {
    // If this method is called that means service is most probably
    // being deleted.
    Glib::Mutex::Lock lock(lock_);
    for(std::list<InfoRegistrar*>::iterator r = regr_.begin();
                              r != regr_.end();++r) {
            (*r)->removeService(reg);
    };
}

// -------------------------------------------------------------------

InfoRegistrar::InfoRegistrar(XMLNode cfg) {
    id_=(std::string)cfg.Attribute("id");
    key_   = (std::string)cfg["KeyPath"];
    cert_  = (std::string)cfg["CertificatePath"];
    proxy_ = (std::string)cfg["ProxyPath"];
    cadir_ = (std::string)cfg["CACertificatesDir"];
    url_ = URL((std::string)cfg["URL"]);
    if(!url_) {
      logger_.msg(ERROR, "Can't recognize URL: %s",(std::string)cfg["URL"]);
    } else {
      //logger_.msg(DEBUG, "InfoRegistrar created for URL: %s",(std::string)cfg["URL"]);
    }

    time_t rawtime;
    time ( &rawtime );  //current time
    gmtime ( &rawtime );
    Time ctime(rawtime);
    creation_time = ctime;
}

bool InfoRegistrar::addService(InfoRegister* reg) {
    Glib::Mutex::Lock lock(lock_);
    for(std::list<Register_Info_Type>::iterator r = reg_.begin();
                                           r!=reg_.end();++r) {
        if(reg == r->p_register) {
            logger_.msg(DEBUG, "InfoRegistrar (%s) service already registered.", url_.fullstr());
            return false;
        }
    };
    Register_Info_Type reg_info;
    reg_info.p_register = reg;
    reg_info.period = reg->getPeriod();
    reg_info.next_registration = creation_time.GetTime() + reg->getPeriod() ;
    reg_.push_back(reg_info);
    logger_.msg(DEBUG, "InfoRegistrar (%s) service added.", url_.fullstr());
    return true;
}

bool InfoRegistrar::removeService(InfoRegister* reg) {
    Glib::Mutex::Lock lock(lock_);
    for(std::list<Register_Info_Type>::iterator r = reg_.begin();
                                           r!=reg_.end();++r) {
        if(reg == r->p_register) {
            reg_.erase(r);
            logger_.msg(DEBUG, "InfoRegistrar (%s) service removed.", url_.fullstr());
            return true;
        };
    };
    logger_.msg(ERROR, "InfoRegistrar (%s) remove service was unsuccessful.", url_.fullstr());
    return false;
}

InfoRegistrar::~InfoRegistrar(void) {
    // Registering thread must be stopped before destructor succeeds
    Glib::Mutex::Lock lock(lock_);
    cond_exit_.signal();
    cond_exited_.wait(lock_);
}

class CondExit {
    private:
        Glib::Cond& cond_;
    public:
        CondExit(Glib::Cond& cond):cond_(cond) { };
        ~CondExit(void) { cond_.signal(); };
};

void InfoRegistrar::registration(void) {
    Glib::Mutex::Lock lock(lock_);
    CondExit cond(cond_exited_);
    std::string isis_name = url_.fullstr();
    while(true) {

        logger_.msg(DEBUG, "Registration starts: %s",isis_name);
        logger_.msg(DEBUG, "reg_.size(): %d",reg_.size());

        if(!url_) {
            logger_.msg(WARNING, "Registrant has no proper URL specified");
            return;
        }
        NS reg_ns;
        reg_ns["glue2"] = GLUE2_D42_NAMESPACE;
        reg_ns["isis"] = ISIS_NAMESPACE;

        // Registration algorithm is stupid and straightforward.
        // This part has to be redone to fit P2P network od ISISes

        for(std::list<Register_Info_Type>::iterator r = reg_.begin();
                                               r!=reg_.end();++r) {
            logger_.msg(DEBUG,"In the for statement");
            XMLNode services_doc(reg_ns, "RegEntry");
            if(!((r->p_register)->getService())) continue;
            (r->p_register)->getService()->RegistrationCollector(services_doc);
            // TODO check the received registration information

            // prepare for sending to ISIS
            logger_.msg(DEBUG, "Registering to %s ISIS", isis_name);
            PayloadSOAP request(reg_ns);
            XMLNode op = request.NewChild("isis:Register");
            XMLNode header = op.NewChild("isis:Header");

            // create header
            time_t rawtime;
            time ( &rawtime );  //current time
            tm * ptm;
            ptm = gmtime ( &rawtime );

            std::string mon_prefix = (ptm->tm_mon+1 < 10)?"0":"";
            std::string day_prefix = (ptm->tm_mday < 10)?"0":"";
            std::string hour_prefix = (ptm->tm_hour < 10)?"0":"";
            std::string min_prefix = (ptm->tm_min < 10)?"0":"";
            std::string sec_prefix = (ptm->tm_sec < 10)?"0":"";
            std::stringstream out;
            out << ptm->tm_year+1900<<"-"<<mon_prefix<<ptm->tm_mon+1<<"-"<<day_prefix<<ptm->tm_mday<<"T"<<hour_prefix<<ptm->tm_hour<<":"<<min_prefix<<ptm->tm_min<<":"<<sec_prefix<<ptm->tm_sec;
            header.NewChild("MessageGenerationTime") = out.str();

            // create body
            op.NewChild(services_doc);

            // send
            PayloadSOAP *response;
            MCCConfig mcc_cfg;
            mcc_cfg.AddPrivateKey(key_);
            mcc_cfg.AddCertificate(cert_);
            mcc_cfg.AddProxy(proxy_);
            mcc_cfg.AddCADir(cadir_);

            std::string services_document;
            services_doc.GetDoc(services_document, true);
            logger_.msg(DEBUG, "RegistrationCollector: %s", services_document);
            logger_.msg(DEBUG, "Call the ISIS.process method.");

            ClientSOAP cli(mcc_cfg,url_);
            MCC_Status status = cli.process(&request, &response);

            //calculate the waiting time
            //std::string s_period_ = (std::string)services_doc["MetaSrcAdv"]["Expiration"];
            if ( bool(services_doc["MetaSrcAdv"]["Expiration"]) ){
               Period time((std::string)services_doc["MetaSrcAdv"]["Expiration"]);
               period_ = time.GetPeriod();
            } else {
               period_ = -1;  //Wath is the default registration time?
            }

            std::string response_string;
            (*response)["RegisterResponse"].GetDoc(response_string, true);
            logger_.msg(DEBUG, "Response from the ISIS: %s", response_string);

            if ((!status) || 
                (!response) || 
                (!bool((*response)["RegisterResponse"]))) {
                logger_.msg(ERROR, "Error during registration to %s ISIS", isis_name);
            } else {
                XMLNode fault = (*response)["Fault"];
                if(!fault)  {
                    logger_.msg(DEBUG, "Successful registration to ISIS (%s)", isis_name); 
                } else {
                    logger_.msg(DEBUG, "Failed to register to ISIS (%s) - %s", isis_name, std::string(fault["Description"])); 
                }
            }
        }
        logger_.msg(DEBUG, "Registration ends: %s",isis_name);
        logger_.msg(DEBUG, "Waiting period is %d second(s).",period_);
        if(period_ <= 0) break; // One time registration
        Glib::TimeVal etime;
        etime.assign_current_time();
        etime.add_milliseconds(period_*1000L);
        // Sleep and exit if interrupted by request to exit
        if(cond_exit_.timed_wait(lock_,etime)) break; 
        //sleep(period_);
    }
    logger_.msg(DEBUG, "Registration exit: %s",isis_name);
}

// -------------------------------------------------------------------

InfoRegisters::InfoRegisters(XMLNode &cfg, Service *service) {
    if(!service) return;
    NS ns;
    ns["iregc"]=REGISTRATION_CONFIG_NAMESPACE;
    cfg.Namespaces(ns);
    for(XMLNode node = cfg["iregc:InfoRegistration"];(bool)node;++node) {
        registers_.push_back(new InfoRegister(node,service));
    }
}

InfoRegisters::~InfoRegisters(void) {
    for(std::list<InfoRegister*>::iterator i = registers_.begin();i!=registers_.end();++i) {
        if(*i) delete (*i);
    }
}

// -------------------------------------------------------------------

} // namespace Arc

