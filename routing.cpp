#include "routing.hpp"
#include <stdexcept>
#include <vector>
namespace quickipc {
void ListenerRegistry::add(std::string l,Listener cb){if(l.empty()||!cb)throw std::invalid_argument("label and callback required");std::lock_guard g(mutex_);listeners_[std::move(l)]=std::move(cb);}
void ListenerRegistry::remove(const std::string&l){std::lock_guard g(mutex_);listeners_.erase(l);}
void ListenerRegistry::dispatch(const std::string&l,const Bytes&p)const{std::vector<Listener> calls;{std::lock_guard g(mutex_);for(auto&[k,cb]:listeners_)if(l==k||(l.size()>k.size()&&l.compare(0,k.size(),k)==0&&l[k.size()]=='.'))calls.push_back(cb);}for(auto&cb:calls)cb(l,p);}
bool ListenerRegistry::claim_reply(std::string l,std::string p,ReplyHandler cb){if(l.empty()||p.empty()||!cb)return false;std::lock_guard g(mutex_);return replies_.emplace(std::move(l),Owner{std::move(p),std::move(cb)}).second;}
void ListenerRegistry::release_reply(const std::string&l,const std::string&p){std::lock_guard g(mutex_);auto i=replies_.find(l);if(i!=replies_.end()&&i->second.process==p)replies_.erase(i);}
Bytes ListenerRegistry::request(const std::string&l,const Bytes&p)const{ReplyHandler cb;{std::lock_guard g(mutex_);auto i=replies_.find(l);if(i==replies_.end())throw std::runtime_error("no reply callback registered");cb=i->second.callback;}return cb(p);}
}
