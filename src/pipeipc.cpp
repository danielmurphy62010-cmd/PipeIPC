#include "pipeipc.hpp"
#include <Windows.h>
#include <bcrypt.h>
#include <blake3.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <bit>
#include <stdexcept>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace pipeipc {
Handle::~Handle(){if(valid())CloseHandle(h_);}
Handle::Handle(Handle&&o)noexcept:h_(std::exchange(o.h_,INVALID_HANDLE_VALUE)){}
Handle&Handle::operator=(Handle&&o)noexcept{if(this!=&o){if(valid())CloseHandle(h_);h_=std::exchange(o.h_,INVALID_HANDLE_VALUE);}return *this;}
HANDLE Handle::release()noexcept{return std::exchange(h_,INVALID_HANDLE_VALUE);}
void write_exact(HANDLE h,const void*p,std::size_t n){auto b=static_cast<const std::uint8_t*>(p);while(n){DWORD done=0,part=DWORD((n>MAXDWORD)?MAXDWORD:n);if(!WriteFile(h,b,part,&done,nullptr)||!done)throw std::runtime_error("named pipe write failed");b+=done;n-=done;}}
void read_exact(HANDLE h,void*p,std::size_t n){auto b=static_cast<std::uint8_t*>(p);while(n){DWORD done=0,part=DWORD((n>MAXDWORD)?MAXDWORD:n);if(!ReadFile(h,b,part,&done,nullptr)||!done)throw std::runtime_error("named pipe read failed");b+=done;n-=done;}}
Handle create_pipe(const std::wstring&name,DWORD mode){HANDLE h=CreateNamedPipeW(name.c_str(),mode,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,1u<<20,1u<<20,0,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("CreateNamedPipeW failed");return Handle(h);}
Handle open_pipe(const std::wstring&name,DWORD access,DWORD timeout){
 const ULONGLONG deadline=GetTickCount64()+timeout;
 do{
  if(!WaitNamedPipeW(name.c_str(),100)){
   DWORD error=GetLastError();
   if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_SEM_TIMEOUT&&error!=ERROR_PIPE_BUSY)throw std::runtime_error("WaitNamedPipeW failed");
   Sleep(10);continue;
  }
  HANDLE h=CreateFileW(name.c_str(),access,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(h!=INVALID_HANDLE_VALUE)return Handle(h);
  DWORD error=GetLastError();
  if(error!=ERROR_PIPE_BUSY&&error!=ERROR_FILE_NOT_FOUND)throw std::runtime_error("CreateFileW failed");
 }while(GetTickCount64()<deadline);
 throw std::runtime_error("timed out waiting for named pipe");
}
}


namespace pipeipc { namespace {
Digest hash(std::span<const std::uint8_t>b){Digest d{};blake3_hasher h;blake3_hasher_init(&h);blake3_hasher_update(&h,b.data(),b.size());blake3_hasher_finalize(&h,d.data(),d.size());return d;}
void add(Bytes&o,const void*p,std::size_t n){auto q=static_cast<const std::uint8_t*>(p);o.insert(o.end(),q,q+n);}
void w32(Bytes&o,std::uint32_t v){for(int i=0;i<4;i++)o.push_back(std::uint8_t(v>>(8*i)));}
void w64(Bytes&o,std::uint64_t v){for(int i=0;i<8;i++)o.push_back(std::uint8_t(v>>(8*i)));}
std::uint32_t r32(const std::uint8_t*p){std::uint32_t v=0;for(int i=0;i<4;i++)v|=std::uint32_t(p[i])<<(8*i);return v;}
std::uint64_t r64(const std::uint8_t*p){std::uint64_t v=0;for(int i=0;i<8;i++)v|=std::uint64_t(p[i])<<(8*i);return v;}
}
ProtocolSecrets random_secrets(){ProtocolSecrets s; if(BCryptGenRandom(nullptr,s.magic1.data(),8,BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0||BCryptGenRandom(nullptr,s.magic2.data(),8,BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)throw std::runtime_error("BCryptGenRandom failed");return s;}
std::array<std::uint8_t,8> listener_hash(const std::string&l){auto d=hash(std::span(reinterpret_cast<const std::uint8_t*>(l.data()),l.size()));std::array<std::uint8_t,8> r{};std::copy_n(d.begin(),8,r.begin());return r;}
Bytes encode_frame(const ProtocolSecrets&s,std::uint64_t id,const std::array<std::uint8_t,8>&lid,std::span<const std::uint8_t>p){if(p.size()>max_payload_size)throw std::runtime_error("payload exceeds 16 MiB");Bytes h;w32(h,std::uint32_t(p.size()));w64(h,id);auto hh=hash(h),ph=hash(p);Bytes o;add(o,s.magic1.data(),8);add(o,h.data(),12);add(o,hh.data(),32);add(o,ph.data(),32);add(o,lid.data(),8);add(o,p.data(),p.size());add(o,s.magic2.data(),8);return o;}
Frame decode_frame(const ProtocolSecrets&s,std::span<const std::uint8_t>w){constexpr std::size_t fixed=100;if(w.size()<fixed||!std::equal(s.magic1.begin(),s.magic1.end(),w.begin()))throw std::runtime_error("bad leading magic");auto n=r32(w.data()+8);if(n>max_payload_size||w.size()!=fixed+n)throw std::runtime_error("invalid frame length");if(!std::equal(s.magic2.begin(),s.magic2.end(),w.end()-8))throw std::runtime_error("bad trailing magic");Frame f;f.header.payload_size=n;f.header.message_id=r64(w.data()+12);std::copy_n(w.data()+20,32,f.header.header_hash.begin());std::copy_n(w.data()+52,32,f.header.payload_hash.begin());std::copy_n(w.data()+84,8,f.header.listener_id.begin());Bytes h;w32(h,n);w64(h,f.header.message_id);if(hash(h)!=f.header.header_hash)throw std::runtime_error("bad header hash");f.payload.assign(w.begin()+92,w.end()-8);if(hash(f.payload)!=f.header.payload_hash)throw std::runtime_error("bad payload hash");return f;}
Bytes encode_ack(const ProtocolSecrets&s,const FrameHeader&h,char status){if(status!='0'&&status!='1'&&status!='2')throw std::runtime_error("invalid ACK status");Bytes o;add(o,s.magic1.data(),8);w32(o,h.payload_size);w64(o,h.message_id);add(o,h.header_hash.data(),32);add(o,h.payload_hash.data(),32);add(o,h.listener_id.data(),8);o.push_back(std::uint8_t(status));add(o,s.magic2.data(),8);return o;}
}


namespace pipeipc {
void ListenerRegistry::add(std::string l,Listener cb){
 if(l.empty()||!cb)throw std::invalid_argument("label and callback required");
 auto item=std::make_shared<Listener>(std::move(cb));
 std::lock_guard g(mutex_);listeners_[std::move(l)]=std::move(item);
}
void ListenerRegistry::remove(const std::string&l){std::lock_guard g(mutex_);listeners_.erase(l);}
bool ListenerRegistry::contains(const std::string&l)const{std::lock_guard g(mutex_);return listeners_.contains(l);}
void ListenerRegistry::dispatch(const std::string&l,const Value&v)const{
 std::vector<std::shared_ptr<Listener>> calls;
 {std::lock_guard g(mutex_);for(auto&[k,cb]:listeners_)if(l==k)calls.push_back(cb);}
 for(auto&cb:calls){try{(*cb)(l,v);}catch(...){}}
}
}


namespace pipeipc {
namespace {
std::wstring base(const Options&o){return L"\\\\.\\pipe\\PipeIPC."+o.name;}
std::wstring ctl(const Options&o){return base(o)+L".control";}
std::wstring peerpipe(const Options&o,const std::string&id,const wchar_t*ch){return base(o)+L"."+std::wstring(id.begin(),id.end())+L"."+ch;}
std::string make_id(){GUID g{};if(CoCreateGuid(&g)!=S_OK)throw std::runtime_error("CoCreateGuid failed");wchar_t w[40]{};if(!StringFromGUID2(g,w,40))throw std::runtime_error("StringFromGUID2 failed");char b[40]{};int n=WideCharToMultiByte(CP_UTF8,0,w,-1,b,sizeof(b),nullptr,nullptr);if(!n)throw std::runtime_error("GUID conversion failed");std::string id(b,std::size_t(n-1));if(id.size()==38&&id.front()=='{'&&id.back()=='}')id=id.substr(1,36);return id;}
Bytes read_frame(HANDLE h,const ProtocolSecrets&s){std::array<std::uint8_t,92> head{};read_exact(h,head.data(),head.size());if(!std::equal(s.magic1.begin(),s.magic1.end(),head.begin()))throw std::runtime_error("bad frame magic");std::uint32_t n=0;for(int i=0;i<4;i++)n|=std::uint32_t(head[8+i])<<(8*i);if(n>max_payload_size)throw std::runtime_error("frame exceeds limit");Bytes wire(head.begin(),head.end());wire.resize(100+n);read_exact(h,wire.data()+92,n+8);return wire;}
void write_wire(HANDLE h,const Bytes&b){write_exact(h,b.data(),b.size());}
constexpr const char* shared_topic="__pipeipc.shared";
const std::array<std::uint8_t,8>& shared_channel(){static const auto id=listener_hash(shared_topic);return id;}
void u32(Bytes&b,std::uint32_t v){for(int i=0;i<4;i++)b.push_back(std::uint8_t(v>>(8*i)));}
std::uint32_t take32(const Bytes&b,std::size_t&p){if(p+4>b.size())throw std::runtime_error("truncated IPC command");std::uint32_t v=0;for(int i=0;i<4;i++)v|=std::uint32_t(b[p++])<<(8*i);return v;}
std::uint64_t take64(const Bytes&b,std::size_t&p){if(p+8>b.size())throw std::runtime_error("truncated IPC command");std::uint64_t v=0;for(int i=0;i<8;i++)v|=std::uint64_t(b[p++])<<(8*i);return v;}
void u64(Bytes&b,std::uint64_t v){for(int i=0;i<8;i++)b.push_back(std::uint8_t(v>>(8*i)));}
void text_blob(Bytes&b,const std::string&v){if(v.size()>max_payload_size)throw std::runtime_error("shared variable key too long");u32(b,std::uint32_t(v.size()));b.insert(b.end(),v.begin(),v.end());}
std::string take_text(const Bytes&b,std::size_t&p){auto n=take32(b,p);if(n>b.size()-p)throw std::runtime_error("invalid shared key length");std::string r(reinterpret_cast<const char*>(b.data()+p),n);p+=n;return r;}
Bytes make_shared_set(const std::string&key,const Value&value){Bytes out{1};text_blob(out,key);auto v=encode_value(value);u32(out,std::uint32_t(v.size()));out.insert(out.end(),v.begin(),v.end());return out;}
Bytes make_shared_delete(const std::string&key){Bytes out{2};text_blob(out,key);return out;}
Bytes make_shared_snapshot_request(){return Bytes{3};}
Bytes make_message_payload(const std::string&label,const Value&value){
 if(label.empty()||label=="__pipeipc.shared")throw std::invalid_argument("invalid or reserved message label");
 Bytes raw=encode_value(value),out;text_blob(out,label);u32(out,std::uint32_t(raw.size()));out.insert(out.end(),raw.begin(),raw.end());
 if(out.size()>max_payload_size)throw std::runtime_error("message payload exceeds 16 MiB");
 return out;
}
Message decode_message_payload(const Bytes&payload){
 std::size_t pos=0;Message message;message.label=take_text(payload,pos);
 if(message.label.empty())throw std::runtime_error("empty message label");
 auto n=take32(payload,pos);if(n>payload.size()-pos||pos+n!=payload.size())throw std::runtime_error("invalid message value length");
 Bytes raw(payload.begin()+pos,payload.end());message.value=decode_value(raw);return message;
}
}
struct Server::Peer {
 std::string id; ProtocolSecrets secrets; Handle tx,rx,txack,rxack;
 std::mutex out_mutex; std::deque<Bytes> out; std::size_t queued_bytes=0;
 std::condition_variable cv; std::atomic_bool live{true},threads_ready{false};
 std::thread reader,writer,ackreader;
};
Server::Server(Options o):options_(std::move(o)),secrets_(random_secrets()){}
Server::~Server(){stop();}
bool Server::running()const noexcept{return running_;}
void Server::start(){bool expected=false;if(!running_.compare_exchange_strong(expected,true))return;reap_thread_=std::thread(&Server::reap_loop,this);accept_thread_=std::thread(&Server::accept_loop,this);}
void Server::stop(){
 if(!running_.exchange(false))return;
 if(accept_thread_.joinable()){CancelSynchronousIo(accept_thread_.native_handle());accept_thread_.join();}
 if(reap_thread_.joinable())reap_thread_.join();
 std::vector<std::shared_ptr<Peer>> peers;
 {std::lock_guard g(peers_mutex_);for(auto&[id,p]:peers_){p->live=false;p->cv.notify_all();peers.push_back(p);}peers_.clear();}
 for(auto&p:peers){if(p->tx.valid())CancelIoEx(p->tx.get(),nullptr);if(p->rx.valid())CancelIoEx(p->rx.get(),nullptr);if(p->txack.valid())CancelIoEx(p->txack.get(),nullptr);if(p->rxack.valid())CancelIoEx(p->rxack.get(),nullptr);}
 for(auto&p:peers){if(p->reader.joinable())p->reader.join();if(p->writer.joinable())p->writer.join();if(p->ackreader.joinable())p->ackreader.join();}
 {std::lock_guard state_lock(shared_mutex_);shared_values_.clear();shared_revision_=0;}
}
void Server::enqueue(const std::shared_ptr<Peer>&p,Bytes b){
 std::lock_guard q(p->out_mutex);
 if(!p->live)return;
 if(p->queued_bytes+b.size()>options_.max_queue_bytes){
  p->live=false;p->cv.notify_all();
  for(auto*h:{&p->tx,&p->rx,&p->txack,&p->rxack})if(h->valid())CancelIoEx(h->get(),nullptr);
  return;
 }
 p->queued_bytes+=b.size();p->out.push_back(std::move(b));p->cv.notify_one();
}
void Server::remove_peer(const std::shared_ptr<Peer>&p){
 p->live=false;p->cv.notify_all();
 for(auto*h:{&p->tx,&p->rx,&p->txack,&p->rxack})if(h->valid())CancelIoEx(h->get(),nullptr);
}
void Server::reap_loop(){
 while(running_){
  std::vector<std::shared_ptr<Peer>> dead;
  {std::lock_guard g(peers_mutex_);for(auto i=peers_.begin();i!=peers_.end();){if(!i->second->live&&i->second->threads_ready){dead.push_back(i->second);i=peers_.erase(i);}else ++i;}}
  for(auto&p:dead){
   if(p->reader.joinable())p->reader.join();
   if(p->writer.joinable())p->writer.join();
   if(p->ackreader.joinable())p->ackreader.join();
  }
  Sleep(25);
 }
}
void Server::broadcast(const Bytes&b){
 std::lock_guard g(peers_mutex_);
 for(auto&[id,p]:peers_)enqueue(p,b);
}
void Server::send(const std::string&label,const Value&value){
 auto payload=make_message_payload(label,value);
 broadcast(encode_frame(secrets_,next_mid_++,listener_hash(label),payload));
}
void Server::handle_shared_command(const std::shared_ptr<Peer>&requester,const Bytes&command){
 if(command.empty())throw std::runtime_error("empty shared-variable command");
 std::size_t pos=1;
 const auto op=command[0];
 std::lock_guard state_lock(shared_mutex_);
 Bytes event;
 if(op==3){
  if(command.size()!=1)throw std::runtime_error("invalid snapshot request");
  event.push_back(0x81);u64(event,shared_revision_);u32(event,std::uint32_t(shared_values_.size()));
  for(const auto&[key,value]:shared_values_){text_blob(event,key);auto v=encode_value(value);u32(event,std::uint32_t(v.size()));event.insert(event.end(),v.begin(),v.end());}
  enqueue(requester,encode_frame(secrets_,next_mid_++,shared_channel(),event));
  return;
 }
 if(op==1){
  auto key=take_text(command,pos);if(key.empty())throw std::runtime_error("empty shared-variable key");
  auto n=take32(command,pos);if(n>command.size()-pos||pos+n!=command.size())throw std::runtime_error("invalid shared value length");
  Bytes raw(command.begin()+pos,command.end());auto value=decode_value(raw);
  shared_values_[key]=std::move(value);++shared_revision_;
  event.push_back(0x82);u64(event,shared_revision_);text_blob(event,key);u32(event,n);event.insert(event.end(),raw.begin(),raw.end());
 }else if(op==2){
  auto key=take_text(command,pos);if(key.empty()||pos!=command.size())throw std::runtime_error("invalid shared-variable delete");
  shared_values_.erase(key);++shared_revision_;
  event.push_back(0x83);u64(event,shared_revision_);text_blob(event,key);
 }else throw std::runtime_error("unknown shared-variable command");
 auto frame=encode_frame(secrets_,next_mid_++,shared_channel(),event);
 broadcast(frame);
}
void Server::accept_loop(){
 while(running_){try{
  auto control=create_pipe(ctl(options_),PIPE_ACCESS_DUPLEX);
  BOOL ok=ConnectNamedPipe(control.get(),nullptr);if(!ok&&GetLastError()!=ERROR_PIPE_CONNECTED)continue;
  std::array<std::uint8_t,64> hello{};read_exact(control.get(),hello.data(),hello.size());
  std::string id(reinterpret_cast<char*>(hello.data()),36);
  auto p=std::make_shared<Peer>();p->id=id;p->secrets=secrets_;
  p->tx=create_pipe(peerpipe(options_,id,L"TX"),PIPE_ACCESS_INBOUND);
  p->rx=create_pipe(peerpipe(options_,id,L"RX"),PIPE_ACCESS_OUTBOUND);
  p->txack=create_pipe(peerpipe(options_,id,L"TX_ACK"),PIPE_ACCESS_INBOUND);
  p->rxack=create_pipe(peerpipe(options_,id,L"RX_ACK"),PIPE_ACCESS_OUTBOUND);
  write_exact(control.get(),secrets_.magic1.data(),8);write_exact(control.get(),secrets_.magic2.data(),8);
  auto attach=[](Handle&h){BOOL x=ConnectNamedPipe(h.get(),nullptr);if(!x&&GetLastError()!=ERROR_PIPE_CONNECTED)throw std::runtime_error("peer pipe connect failed");};
  attach(p->tx);attach(p->rx);attach(p->txack);attach(p->rxack);
  {std::lock_guard g(peers_mutex_);peers_[id]=p;}
  p->writer=std::thread([this,p]{while(p->live){Bytes b;{std::unique_lock l(p->out_mutex);p->cv.wait(l,[&]{return !p->live||!p->out.empty();});if(!p->live)break;b=std::move(p->out.front());p->out.pop_front();p->queued_bytes-=b.size();}try{write_wire(p->rx.get(),b);}catch(...){remove_peer(p);break;}}remove_peer(p);});
  p->reader=std::thread([this,p]{while(p->live){try{
   auto b=read_frame(p->tx.get(),p->secrets);auto f=decode_frame(p->secrets,b);
   write_wire(p->rxack.get(),encode_ack(p->secrets,f.header,'1'));write_wire(p->rxack.get(),encode_ack(p->secrets,f.header,'2'));
   if(f.header.listener_id==shared_channel()){try{handle_shared_command(p,f.payload);}catch(...){}}
   else broadcast(b);
  }catch(...){remove_peer(p);break;}}
  remove_peer(p);
 });
  p->ackreader=std::thread([this,p]{while(p->live){try{std::array<std::uint8_t,101>a{};read_exact(p->txack.get(),a.data(),a.size());}catch(...){remove_peer(p);break;}}remove_peer(p);});
  p->threads_ready=true;
 }catch(...){if(!running_)break;Sleep(20);}}
}
Client::Client(Options o):options_(std::move(o)),id_(make_id()){}
Client::~Client(){disconnect();}
bool Client::connected()const noexcept{return connected_;}
void Client::connect(){
 if(connected_)return;
 disconnect();
 id_=make_id();
 auto c=open_pipe(ctl(options_),GENERIC_READ|GENERIC_WRITE);
 std::array<std::uint8_t,64> hello{};std::copy(id_.begin(),id_.end(),hello.begin());write_exact(c.get(),hello.data(),hello.size());
 read_exact(c.get(),secrets_.magic1.data(),8);read_exact(c.get(),secrets_.magic2.data(),8);
 auto tx=open_pipe(peerpipe(options_,id_,L"TX"),GENERIC_WRITE);
 auto rx=open_pipe(peerpipe(options_,id_,L"RX"),GENERIC_READ);
 auto ta=open_pipe(peerpipe(options_,id_,L"TX_ACK"),GENERIC_WRITE);
 auto ra=open_pipe(peerpipe(options_,id_,L"RX_ACK"),GENERIC_READ);
 tx_=std::make_unique<Handle>(std::move(tx));rx_=std::make_unique<Handle>(std::move(rx));
 txack_=std::make_unique<Handle>(std::move(ta));rxack_=std::make_unique<Handle>(std::move(ra));
 {std::lock_guard lock(inbox_mutex_);inbox_.clear();inbox_bytes_=0;}
 connected_=true;rx_thread_=std::thread(&Client::receive_loop,this);ack_thread_=std::thread(&Client::ack_loop,this);
}
void Client::disconnect(){
 connected_=false;
 for(auto*h:{tx_.get(),rx_.get(),txack_.get(),rxack_.get()})if(h&&h->valid())CancelIoEx(h->get(),nullptr);
 if(rx_thread_.joinable())rx_thread_.join();if(ack_thread_.joinable())ack_thread_.join();
 tx_.reset();rx_.reset();txack_.reset();rxack_.reset();
 inbox_cv_.notify_all();
}
void Client::on(std::string label,Listener cb){
 if(label.empty()||!cb||label=="__pipeipc.shared")throw std::invalid_argument("invalid or reserved message subscription");
 listeners_.add(std::move(label),std::move(cb));
}
void Client::off(const std::string&label){
 if(label=="__pipeipc.shared")throw std::invalid_argument("reserved PipeIPC label");
 listeners_.remove(label);
}
void Client::send(const std::string&label,const Value&value){
 if(!connected_)throw std::runtime_error("disconnected");
 auto payload=make_message_payload(label,value);
 auto frame=encode_frame(secrets_,next_mid_++,listener_hash(label),payload);
 std::lock_guard lock(send_mutex_);write_wire(tx_->get(),frame);
}
std::optional<Message> Client::recv(std::chrono::milliseconds timeout){
 std::unique_lock lock(inbox_mutex_);
 auto ready=[this]{return !inbox_.empty()||!connected_;};
 bool signaled;
 if(timeout==std::chrono::milliseconds::max()){inbox_cv_.wait(lock,ready);signaled=true;}
 else signaled=inbox_cv_.wait_for(lock,timeout,ready);
 if(!signaled||inbox_.empty())return std::nullopt;
 Message message=std::move(inbox_.front());inbox_.pop_front();
 inbox_bytes_-=encode_value(message.value).size();
 return message;
}
void Client::send_shared(const Bytes&payload){
 if(!connected_)throw std::runtime_error("disconnected");
 auto frame=encode_frame(secrets_,next_mid_++,shared_channel(),payload);
 std::lock_guard lock(send_mutex_);write_wire(tx_->get(),frame);
}
void Client::on_shared(SharedListener callback){
 bool expected=false;if(!shared_subscribed_.compare_exchange_strong(expected,true))throw std::runtime_error("only one SharedVars object is allowed per Client");
 {std::lock_guard lock(shared_listener_mutex_);shared_listener_=std::move(callback);}
}
void Client::off_shared(){
 if(!shared_subscribed_.exchange(false))return;
 std::lock_guard lock(shared_listener_mutex_);shared_listener_={};
}
void Client::receive_loop(){
 while(connected_){try{
  auto wire=read_frame(rx_->get(),secrets_);auto frame=decode_frame(secrets_,wire);
  if(frame.header.listener_id==shared_channel()){
   SharedListener callback;{std::lock_guard lock(shared_listener_mutex_);callback=shared_listener_;}
   if(callback)callback(frame.payload);
  }else{
   auto message=decode_message_payload(frame.payload);
   if(listener_hash(message.label)!=frame.header.listener_id)throw std::runtime_error("message label/hash mismatch");
   if(listeners_.contains(message.label)){
    listeners_.dispatch(message.label,message.value);
   }else{
    const auto size=encode_value(message.value).size();
    {std::lock_guard lock(inbox_mutex_);
     if(inbox_bytes_+size>options_.max_queue_bytes)throw std::runtime_error("client receive queue limit exceeded");
     inbox_bytes_+=size;inbox_.push_back(std::move(message));
    }
    inbox_cv_.notify_one();
   }
  }
  write_wire(txack_->get(),encode_ack(secrets_,frame.header,'1'));
  write_wire(txack_->get(),encode_ack(secrets_,frame.header,'2'));
 }catch(...){if(connected_)connected_=false;inbox_cv_.notify_all();break;}}
}
}


namespace pipeipc {
SharedVars::SharedVars(Client&client,std::chrono::milliseconds timeout):client_(&client){
 if(!client.connected())throw std::runtime_error("client must be connected before creating SharedVars");
 client_->on_shared([this](const Bytes&b){receive(b);});
 try{
  client_->send_shared(make_shared_snapshot_request());
  std::unique_lock lock(mutex_);
  if(!cv_.wait_for(lock,timeout,[this]{return ready_;}))throw std::runtime_error("timed out waiting for shared-variable snapshot");
 }catch(...){client_->off_shared();throw;}
}
SharedVars::~SharedVars(){if(client_)client_->off_shared();}
void SharedVars::receive(const Bytes&b){
 try{
  if(b.empty())return;std::size_t pos=1;auto op=b[0];
  std::lock_guard lock(mutex_);
  if(op==0x81){
   auto rev=take64(b,pos);auto count=take32(b,pos);std::map<std::string,Value> fresh;
   if(count>max_payload_size)throw std::runtime_error("shared snapshot too large");
   while(count--){auto key=take_text(b,pos);auto n=take32(b,pos);if(n>b.size()-pos)throw std::runtime_error("invalid snapshot value length");Bytes raw(b.begin()+pos,b.begin()+pos+n);pos+=n;fresh.emplace(std::move(key),decode_value(raw));}
   if(pos!=b.size())throw std::runtime_error("trailing snapshot data");
   if(rev>=revision_){vars_=std::move(fresh);revision_=rev;}
   ready_=true;cv_.notify_all();return;
  }
  if(op==0x82){
   auto rev=take64(b,pos);auto key=take_text(b,pos);auto n=take32(b,pos);if(n>b.size()-pos||pos+n!=b.size())throw std::runtime_error("invalid shared update");
   if(rev>revision_){Bytes raw(b.begin()+pos,b.end());vars_[key]=decode_value(raw);revision_=rev;}cv_.notify_all();return;
  }
  if(op==0x83){
   auto rev=take64(b,pos);auto key=take_text(b,pos);if(pos!=b.size())throw std::runtime_error("invalid shared delete");
   if(rev>revision_){vars_.erase(key);revision_=rev;}cv_.notify_all();return;
  }
 }catch(...){}
}
void SharedVars::write(std::string key,Value value){
 if(key.empty())throw std::invalid_argument("shared variable key cannot be empty");
 auto encoded=make_shared_set(key,value);
 std::lock_guard lock(mutex_);auto old=vars_.find(key);std::optional<Value> previous;if(old!=vars_.end())previous=old->second;
 vars_[key]=std::move(value);
 try{client_->send_shared(encoded);}catch(...){if(previous)vars_[key]=std::move(*previous);else vars_.erase(key);throw;}
}
Value SharedVars::read(const std::string&key)const{std::lock_guard lock(mutex_);auto i=vars_.find(key);if(i==vars_.end())throw std::out_of_range("unknown shared variable");return i->second;}
void SharedVars::erase(const std::string&key){
 if(key.empty())throw std::invalid_argument("shared variable key cannot be empty");
 Bytes command=make_shared_delete(key);std::lock_guard lock(mutex_);auto old=vars_.find(key);std::optional<Value> previous;if(old!=vars_.end())previous=old->second;vars_.erase(key);
 try{client_->send_shared(command);}catch(...){if(previous)vars_[key]=std::move(*previous);throw;}
}
bool SharedVars::contains(const std::string&key)const{std::lock_guard lock(mutex_);return vars_.contains(key);}
std::map<std::string,Value> SharedVars::snapshot()const{std::lock_guard lock(mutex_);return vars_;}
bool SharedVars::ready()const{std::lock_guard lock(mutex_);return ready_;}
std::uint64_t SharedVars::revision()const{std::lock_guard lock(mutex_);return revision_;}
}


namespace pipeipc { namespace {
void put(Bytes&o,std::uint64_t v){for(int i=0;i<8;i++)o.push_back(std::uint8_t(v>>(8*i)));}
std::uint64_t get(const Bytes&b,std::size_t&p){if(p+8>b.size())throw std::runtime_error("truncated value");std::uint64_t v=0;for(int i=0;i<8;i++)v|=std::uint64_t(b[p++])<<(8*i);return v;}
void blob(Bytes&o,const Bytes&b){put(o,b.size());o.insert(o.end(),b.begin(),b.end());}
Bytes unblob(const Bytes&b,std::size_t&p){auto n=get(b,p);if(n>b.size()-p)throw std::runtime_error("invalid blob length");Bytes r(b.begin()+p,b.begin()+p+n);p+=n;return r;}
void enc(Bytes&,const Value&); Value dec(const Bytes&,std::size_t&);
}
bool List::operator<(const List&o)const{return values<o.values;}
bool Tuple::operator<(const Tuple&o)const{return values<o.values;}
bool Set::operator<(const Set&o)const{return values<o.values;}
bool Dict::operator<(const Dict&o)const{return values<o.values;}
bool Value::operator<(const Value&o)const {if(data.index()!=o.data.index())return data.index()<o.data.index();return data<o.data;}
namespace {
void enc(Bytes&o,const Value&v){o.push_back(std::uint8_t(v.data.index()));std::visit([&](const auto&x){using T=std::decay_t<decltype(x)>;
 if constexpr(std::is_same_v<T,std::monostate>){}
 else if constexpr(std::is_same_v<T,std::int64_t>||std::is_same_v<T,double>)put(o,std::bit_cast<std::uint64_t>(x));
 else if constexpr(std::is_same_v<T,bool>)o.push_back(x?1:0);
 else if constexpr(std::is_same_v<T,Bytes>)blob(o,x);
 else if constexpr(std::is_same_v<T,std::string>)blob(o,Bytes(x.begin(),x.end()));
 else if constexpr(std::is_same_v<T,List>||std::is_same_v<T,Tuple>){put(o,x.values.size());for(auto&e:x.values)enc(o,e);}
 else if constexpr(std::is_same_v<T,Set>){put(o,x.values.size());for(auto&e:x.values)enc(o,e);}
 else if constexpr(std::is_same_v<T,Dict>){put(o,x.values.size());for(auto&[k,val]:x.values){blob(o,Bytes(k.begin(),k.end()));enc(o,val);}}
},v.data);}
Value dec(const Bytes&b,std::size_t&p){if(p>=b.size())throw std::runtime_error("missing value tag");auto t=ValueTag(b[p++]);
 switch(t){
 case ValueTag::None:return Value{};
 case ValueTag::Int64:return Value(std::bit_cast<std::int64_t>(get(b,p)));
 case ValueTag::Float64:return Value(std::bit_cast<double>(get(b,p)));
 case ValueTag::Bool:if(p>=b.size()||b[p]>1)throw std::runtime_error("invalid bool");return Value(bool(b[p++]));
 case ValueTag::Bytes:return Value(unblob(b,p));
 case ValueTag::String:{auto x=unblob(b,p);return Value(std::string(x.begin(),x.end()));}
 case ValueTag::List:case ValueTag::Tuple:case ValueTag::Set:{auto n=get(b,p);if(n>max_payload_size)throw std::runtime_error("container too large");
  if(t==ValueTag::List){List x;while(n--)x.values.push_back(dec(b,p));return Value(std::move(x));}
  if(t==ValueTag::Tuple){Tuple x;while(n--)x.values.push_back(dec(b,p));return Value(std::move(x));}
  Set x;while(n--)x.values.insert(dec(b,p));return Value(std::move(x));}
 case ValueTag::Dict:{auto n=get(b,p);if(n>max_payload_size)throw std::runtime_error("dict too large");Dict x;while(n--){auto k=unblob(b,p);if(!x.values.emplace(std::string(k.begin(),k.end()),dec(b,p)).second)throw std::runtime_error("duplicate dict key");}return Value(std::move(x));}
 default:throw std::runtime_error("unknown value tag");}}
}
Bytes encode_value(const Value&v){Bytes b;enc(b,v);if(b.size()>max_payload_size)throw std::runtime_error("value exceeds 16 MiB");return b;}
Value decode_value(const Bytes&b){std::size_t p=0;auto v=dec(b,p);if(p!=b.size())throw std::runtime_error("trailing value bytes");return v;}
}

