#pragma once
#include "quickipc/value.hpp"
#include <functional>
#include <map>
#include <mutex>
#include <string>
namespace quickipc {
using Listener=std::function<void(const std::string&,const Bytes&)>;
using ReplyHandler=std::function<Bytes(const Bytes&)>;
class ListenerRegistry {
public:
 void add(std::string,Listener); void remove(const std::string&);
 void dispatch(const std::string&,const Bytes&)const;
 bool claim_reply(std::string,std::string,ReplyHandler);
 void release_reply(const std::string&,const std::string&);
 Bytes request(const std::string&,const Bytes&)const;
private:
 mutable std::mutex mutex_; std::map<std::string,Listener> listeners_;
 struct Owner { std::string process; ReplyHandler callback; };
 std::map<std::string,Owner> replies_;
};
}
