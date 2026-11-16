#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace pipeipc {
using Bytes=std::vector<std::uint8_t>;
struct Value;
struct List { std::vector<Value> values; bool operator<(const List&)const; };
struct Tuple { std::vector<Value> values; bool operator<(const Tuple&)const; };
struct Set { std::set<Value> values; bool operator<(const Set&)const; };
struct Dict { std::map<std::string,Value> values; bool operator<(const Dict&)const; };
struct Value {
 using Data=std::variant<std::monostate,Dict,List,Tuple,Set,std::int64_t,double,bool,Bytes,std::string>;
 Data data;
 Value()=default;
 template<class T> Value(T v):data(std::move(v)){}
 bool operator<(const Value&)const;
};
enum class ValueTag:std::uint8_t { None=0,Dict=1,List=2,Tuple=3,Set=4,Int64=5,Float64=6,Bool=7,Bytes=8,String=9 };
Bytes encode_value(const Value&);
Value decode_value(const Bytes&);

class Handle {
public:
 explicit Handle(HANDLE h=INVALID_HANDLE_VALUE):h_(h){}
 ~Handle(); Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
 Handle(Handle&&)noexcept; Handle& operator=(Handle&&)noexcept;
 HANDLE get()const noexcept{return h_;} bool valid()const noexcept{return h_!=INVALID_HANDLE_VALUE;}
 HANDLE release()noexcept;
private: HANDLE h_;
};
void write_exact(HANDLE,const void*,std::size_t);
void read_exact(HANDLE,void*,std::size_t);
Handle create_pipe(const std::wstring&,DWORD open_mode);
Handle open_pipe(const std::wstring&,DWORD access,DWORD timeout_ms=10000);

inline constexpr std::uint32_t max_payload_size=16u*1024u*1024u;
using Magic=std::array<std::uint8_t,8>;
using Digest=std::array<std::uint8_t,32>;
struct FrameHeader { std::uint32_t payload_size{}; std::uint64_t message_id{}; Digest header_hash{},payload_hash{}; std::array<std::uint8_t,8> listener_id{}; };
struct Frame { FrameHeader header; Bytes payload; };
struct ProtocolSecrets { Magic magic1{},magic2{}; };
ProtocolSecrets random_secrets();
std::array<std::uint8_t,8> listener_hash(const std::string&);
Bytes encode_frame(const ProtocolSecrets&,std::uint64_t,const std::array<std::uint8_t,8>&,std::span<const std::uint8_t>);
Frame decode_frame(const ProtocolSecrets&,std::span<const std::uint8_t>);
Bytes encode_ack(const ProtocolSecrets&,const FrameHeader&,char);

using Listener=std::function<void(const std::string&,const Bytes&)>;
class ListenerRegistry {
public:
 void add(std::string,Listener); void remove(const std::string&);
 void dispatch(const std::string&,const Bytes&)const;
private:
 mutable std::mutex mutex_; std::map<std::string,std::shared_ptr<Listener>> listeners_;
};

struct Options { std::wstring name=L"default"; std::size_t max_queue_bytes=256u*1024u*1024u; };
class Client;
class SharedVars {
public:
 explicit SharedVars(Client&,std::chrono::milliseconds timeout=std::chrono::seconds(10));
 ~SharedVars();
 SharedVars(const SharedVars&)=delete; SharedVars& operator=(const SharedVars&)=delete;
 void write(std::string,Value);
 Value read(const std::string&)const;
 void erase(const std::string&);
 bool contains(const std::string&)const;
 std::map<std::string,Value> snapshot()const;
 bool ready()const;
 std::uint64_t revision()const;
private:
 void receive(const Bytes&);
 Client* client_;
 mutable std::mutex mutex_;
 std::condition_variable cv_;
 std::map<std::string,Value> vars_;
 std::uint64_t revision_=0;
 bool ready_=false;
};

class Server {
public:
 explicit Server(Options={}); ~Server();
 Server(const Server&)=delete; Server& operator=(const Server&)=delete;
 void start(); void stop(); bool running()const noexcept;
 void publish(const std::string& label,const Bytes& payload);
 void send(const std::string& label,const Value& value);
private:
 struct Peer;
 void accept_loop(); void reap_loop();
 void enqueue(const std::shared_ptr<Peer>&,Bytes);
 void remove_peer(const std::shared_ptr<Peer>&);
 void broadcast(const Bytes&);
 void handle_shared_command(const std::shared_ptr<Peer>&,const Bytes&);
 Options options_; ProtocolSecrets secrets_; std::atomic_bool running_{false};
 std::thread accept_thread_,reap_thread_; std::mutex peers_mutex_; std::unordered_map<std::string,std::shared_ptr<Peer>> peers_;
 std::atomic_uint64_t next_mid_{1};
 std::mutex shared_mutex_; std::map<std::string,Value> shared_values_; std::uint64_t shared_revision_=0;
};
class Client {
public:
 explicit Client(Options={}); ~Client();
 Client(const Client&)=delete; Client& operator=(const Client&)=delete;
 void connect(); void disconnect(); bool connected()const noexcept;
 void on(std::string label,Listener cb); void off(const std::string&);
 void publish(const std::string&,const Bytes&);
 void send(const std::string&,const Value&);
 ListenerRegistry& listeners()noexcept{return listeners_;}
 const std::string& id()const noexcept{return id_;}
private:
 void receive_loop(); void ack_loop();
 void publish_shared(const Bytes&);
 void on_shared(Listener);
 void off_shared();
 friend class SharedVars;
 Options options_; std::string id_; ProtocolSecrets secrets_{};
 std::atomic_bool connected_{false}; std::thread rx_thread_,ack_thread_;
 std::unique_ptr<Handle> tx_,rx_,txack_,rxack_;
 ListenerRegistry listeners_; std::mutex send_mutex_; std::mutex label_mutex_;
 std::map<std::array<std::uint8_t,8>,std::string> labels_;
 std::atomic_uint64_t next_mid_{1};
};
}
