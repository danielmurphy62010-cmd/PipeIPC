#pragma once
#include "quickipc/protocol.hpp"
#include "quickipc/routing.hpp"
#include "quickipc/shared_vars.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
namespace quickipc {
struct Options { std::wstring name=L"default"; std::size_t max_queue_bytes=256u*1024u*1024u; };
class Server {
public:
 explicit Server(Options={}); ~Server();
 Server(const Server&)=delete; Server& operator=(const Server&)=delete;
 void start(); void stop(); bool running()const noexcept;
 void publish(const std::string& label,const Bytes& payload);
private: struct Peer; void accept_loop(); void peer_loop(std::shared_ptr<Peer>);
 Options options_; ProtocolSecrets secrets_; std::atomic_bool running_{false};
 std::thread accept_thread_; std::mutex peers_mutex_; std::unordered_map<std::string,std::shared_ptr<Peer>> peers_;
 std::atomic_uint64_t next_mid_{1};
};
class Client {
public:
 explicit Client(Options={}); ~Client();
 Client(const Client&)=delete; Client& operator=(const Client&)=delete;
 void connect(); void disconnect(); bool connected()const noexcept;
 void on(std::string label,Listener cb); void off(const std::string&);
 void publish(const std::string&,const Bytes&);
 void request(const std::string&,const Bytes&,std::function<void(Bytes)> callback);
 bool claim_reply(const std::string&,ReplyHandler);
 void release_reply(const std::string&);
 ListenerRegistry& listeners()noexcept{return listeners_;}
 const std::string& id()const noexcept{return id_;}
private:
 void receive_loop(); void ack_loop();
 Options options_; std::string id_; ProtocolSecrets secrets_{};
 std::atomic_bool connected_{false}; std::thread rx_thread_,ack_thread_;
 ListenerRegistry listeners_; std::mutex send_mutex_; std::mutex pending_mutex_;
 std::unordered_map<std::uint64_t,std::function<void(Bytes)>> pending_;
 std::atomic_uint64_t next_mid_{1};
};
}
