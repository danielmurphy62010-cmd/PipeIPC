#include "quickipc/shared_vars.hpp"
#include <stdexcept>
namespace quickipc{
SharedVars::SharedVars(std::string l,std::string p,bool c):label_(std::move(l)),process_id_(std::move(p)),creator_(c){if(label_.empty()||process_id_.empty())throw std::invalid_argument("label and process id required");}
void SharedVars::set(std::string k,Value v){if(!creator_)throw std::runtime_error("only creator may mutate authoritative values");std::lock_guard g(mutex_);vars_[std::move(k)]=std::move(v);}
Value SharedVars::get(const std::string&k)const{std::lock_guard g(mutex_);auto i=vars_.find(k);if(i==vars_.end())throw std::out_of_range("unknown variable");return i->second;}
std::map<std::string,Value> SharedVars::snapshot()const{std::lock_guard g(mutex_);return vars_;}
void SharedVars::join(const std::map<std::string,Value>&s){if(creator_)throw std::runtime_error("creator cannot join itself");std::lock_guard g(mutex_);vars_=s;}
std::map<std::string,Value> SharedVars::handle_variable_request()const{if(!creator_)throw std::runtime_error("only creator handles variable requests");return snapshot();}
}
