#include "quickipc/routing.hpp"
#include <stdexcept>
namespace quickipc{
void ListenerRegistry::add(std::string l,Listener cb){if(l.empty()||!cb)throw std::invalid_argument("listener/callback required");std::lock_guard g(mutex_);listeners_[std::move(l)]=std::move(cb);}
void ListenerRegistry::remove(const std::string&l){std::lock_guard g(mutex_);listeners_.erase(l);}
void ListenerRegistry::dispatch(const std::string&l,const Bytes&p)const{std::vector<std::pair<std::string,Listener>> v;{std::lock_guard g(mutex_);for(auto&[k,cb]:listeners_)if(l==k||(l.size()>k.size()&&l.compare(0,k.size(),k)==0&&l[k.size()]=='.'))v.emplace_back(k,cb);}for(auto&[k,cb]:v)cb(l,p);}
bool ListenerRegistry::claim_reply(std::string l,std::string owner,ReplyHandler cb){if(l.empty()||owner.empty()||!cb)return false;std::lock_guard g(mutex_);return replies_.emplace(std::move(l),ReplyOwner{std::move(owner),std::move(cb)}).second;}
void ListenerRegistry::release_reply(const std::string&l,const std::string&o){std::lock_guard g(mutex_);auto i=replies_.find(l);if(i!=replies_.end()&&i->second.process_id==o)replies_.erase(i);}
Bytes ListenerRegistry::request(const std::string&l,const Bytes&p)const{ReplyHandler cb;{std::lock_guard g(mutex_);auto i=replies_.find(l);if(i==replies_.end())throw std::runtime_error("no reply handler");cb=i->second.callback;}return cb(p);}
}
