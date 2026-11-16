#include "shared_vars.hpp"
#include <stdexcept>
namespace quickipc {
SharedVars::SharedVars(std::string l,std::string p,bool c):label_(std::move(l)),process_(std::move(p)),creator_(c){if(label_.empty()||process_.empty())throw std::invalid_argument("label and process id required");}
void SharedVars::write(std::string k,Value v){if(!creator_)throw std::runtime_error("only creator may mutate shared variables");if(k.empty()||k.front()=='_')throw std::invalid_argument("invalid shared variable name");std::lock_guard g(mutex_);vars_[std::move(k)]=std::move(v);}
Value SharedVars::read(const std::string&k)const{std::lock_guard g(mutex_);auto i=vars_.find(k);if(i==vars_.end())throw std::out_of_range("unknown shared variable");return i->second;}
void SharedVars::erase(const std::string&k){if(!creator_)throw std::runtime_error("only creator may mutate shared variables");std::lock_guard g(mutex_);vars_.erase(k);}
bool SharedVars::contains(const std::string&k)const{std::lock_guard g(mutex_);return vars_.contains(k);}
std::map<std::string,Value> SharedVars::snapshot()const{std::lock_guard g(mutex_);return vars_;}
void SharedVars::install_snapshot(std::map<std::string,Value>s){if(creator_)throw std::runtime_error("creator cannot install remote snapshot");std::lock_guard g(mutex_);vars_=std::move(s);}
std::map<std::string,Value> SharedVars::handle_snapshot_request()const{if(!creator_)throw std::runtime_error("only creator handles variable requests");return snapshot();}
}
