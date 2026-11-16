#include "pipeipc.hpp"
#include <Windows.h>
#include <bcrypt.h>
#include <blake3.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <bit>
#include <stdexcept>
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
Handle open_pipe(const std::wstring&name,DWORD access,DWORD timeout){if(!WaitNamedPipeW(name.c_str(),timeout))throw std::runtime_error("WaitNamedPipeW failed");HANDLE h=CreateFileW(name.c_str(),access,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("CreateFileW failed");return Handle(h);}
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
void ListenerRegistry::add(std::string l,Listener cb){if(l.empty()||!cb)throw std::invalid_argument("label and callback required");std::lock_guard g(mutex_);listeners_[std::move(l)]=std::move(cb);}
void ListenerRegistry::remove(const std::string&l){std::lock_guard g(mutex_);listeners_.erase(l);}
void ListenerRegistry::dispatch(const std::string&l,const Bytes&p)const{std::vector<Listener> calls;{std::lock_guard g(mutex_);for(auto&[k,cb]:listeners_)if(l==k||(l.size()>k.size()&&l.compare(0,k.size(),k)==0&&l[k.size()]=='.'))calls.push_back(cb);}for(auto&cb:calls)cb(l,p);}
bool ListenerRegistry::claim_reply(std::string l,std::string p,ReplyHandler cb){if(l.empty()||p.empty()||!cb)return false;std::lock_guard g(mutex_);return replies_.emplace(std::move(l),Owner{std::move(p),std::move(cb)}).second;}
void ListenerRegistry::release_reply(const std::string&l,const std::string&p){std::lock_guard g(mutex_);auto i=replies_.find(l);if(i!=replies_.end()&&i->second.process==p)replies_.erase(i);}
Bytes ListenerRegistry::request(const std::string&l,const Bytes&p)const{ReplyHandler cb;{std::lock_guard g(mutex_);auto i=replies_.find(l);if(i==replies_.end())throw std::runtime_error("no reply callback registered");cb=i->second.callback;}return cb(p);}
}


namespace pipeipc {
namespace {
std::wstring base(const Options&o){return L"\\\\.\\pipe\\PipeIPC."+o.name;}
std::wstring ctl(const Options&o){return base(o)+L".control";}
std::wstring peerpipe(const Options&o,const std::string&id,const wchar_t*ch){return base(o)+L"."+std::wstring(id.begin(),id.end())+L"."+ch;}
std::string make_id(){GUID g{};if(CoCreateGuid(&g)!=S_OK)throw std::runtime_error("CoCreateGuid failed");wchar_t w[40]{};if(!StringFromGUID2(g,w,40))throw std::runtime_error("StringFromGUID2 failed");char b[40]{};int n=WideCharToMultiByte(CP_UTF8,0,w,-1,b,sizeof(b),nullptr,nullptr);if(!n)throw std::runtime_error("GUID conversion failed");return std::string(b,std::size_t(n-1));}
Bytes read_frame(HANDLE h,const ProtocolSecrets&s){std::array<std::uint8_t,92> head{};read_exact(h,head.data(),head.size());if(!std::equal(s.magic1.begin(),s.magic1.end(),head.begin()))throw std::runtime_error("bad frame magic");std::uint32_t n=0;for(int i=0;i<4;i++)n|=std::uint32_t(head[8+i])<<(8*i);if(n>max_payload_size)throw std::runtime_error("frame exceeds limit");Bytes wire(head.begin(),head.end());wire.resize(100+n);read_exact(h,wire.data()+92,n+8);return wire;}
void write_wire(HANDLE h,const Bytes&b){write_exact(h,b.data(),b.size());}
}
struct Server::Peer {
 std::string id; ProtocolSecrets secrets; Handle tx,rx,txack,rxack;
 std::mutex out_mutex; std::deque<Bytes> out; std::size_t queued_bytes=0;
 std::condition_variable cv; std::atomic_bool live{true};
 std::thread reader,writer,ackreader;
};
Server::Server(Options o):options_(std::move(o)),secrets_(random_secrets()){}
Server::~Server(){stop();}
bool Server::running()const noexcept{return running_;}
void Server::start(){bool expected=false;if(!running_.compare_exchange_strong(expected,true))return;accept_thread_=std::thread(&Server::accept_loop,this);}
void Server::stop(){
 if(!running_.exchange(false))return;
 if(accept_thread_.joinable()){CancelSynchronousIo(accept_thread_.native_handle());accept_thread_.join();}
 std::vector<std::shared_ptr<Peer>> peers;
 {std::lock_guard g(peers_mutex_);for(auto&[id,p]:peers_){p->live=false;p->cv.notify_all();peers.push_back(p);}peers_.clear();}
 for(auto&p:peers){if(p->tx.valid())CancelIoEx(p->tx.get(),nullptr);if(p->rx.valid())CancelIoEx(p->rx.get(),nullptr);if(p->txack.valid())CancelIoEx(p->txack.get(),nullptr);if(p->rxack.valid())CancelIoEx(p->rxack.get(),nullptr);}
 for(auto&p:peers){if(p->reader.joinable())p->reader.join();if(p->writer.joinable())p->writer.join();if(p->ackreader.joinable())p->ackreader.join();}
}
void Server::publish(const std::string&label,const Bytes&payload){
 auto frame=encode_frame(secrets_,next_mid_++,listener_hash(label),payload);
 std::lock_guard g(peers_mutex_);
 for(auto&[id,p]:peers_){std::lock_guard q(p->out_mutex);if(p->queued_bytes+frame.size()>options_.max_queue_bytes){p->live=false;p->cv.notify_all();continue;}p->queued_bytes+=frame.size();p->out.push_back(frame);p->cv.notify_one();}
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
  p->writer=std::thread([this,p]{while(p->live){Bytes b;{std::unique_lock l(p->out_mutex);p->cv.wait(l,[&]{return !p->live||!p->out.empty();});if(!p->live)break;b=std::move(p->out.front());p->out.pop_front();p->queued_bytes-=b.size();}try{write_wire(p->rx.get(),b);}catch(...){p->live=false;break;}}});
  p->reader=std::thread([this,p]{while(p->live){try{auto b=read_frame(p->tx.get(),p->secrets);auto f=decode_frame(p->secrets,b);auto ack=encode_ack(p->secrets,f.header,'1');write_wire(p->rxack.get(),ack);write_wire(p->rxack.get(),encode_ack(p->secrets,f.header,'2'));std::lock_guard g(peers_mutex_);for(auto&[id,q]:peers_){std::lock_guard l(q->out_mutex);if(q->queued_bytes+b.size()>options_.max_queue_bytes){q->live=false;q->cv.notify_all();continue;}q->out.push_back(b);q->queued_bytes+=b.size();q->cv.notify_one();}}catch(...){p->live=false;p->cv.notify_all();break;}}});
  p->ackreader=std::thread([p]{while(p->live){try{std::array<std::uint8_t,101>a{};read_exact(p->txack.get(),a.data(),a.size());}catch(...){p->live=false;break;}}});
 }catch(...){if(!running_)break;Sleep(20);}}
}
Client::Client(Options o):options_(std::move(o)),id_(make_id()){}
Client::~Client(){disconnect();}
bool Client::connected()const noexcept{return connected_;}
void Client::connect(){
 if(connected_)return;
 auto c=open_pipe(ctl(options_),GENERIC_READ|GENERIC_WRITE);
 std::array<std::uint8_t,64> hello{};std::copy(id_.begin(),id_.end(),hello.begin());write_exact(c.get(),hello.data(),hello.size());
 read_exact(c.get(),secrets_.magic1.data(),8);read_exact(c.get(),secrets_.magic2.data(),8);
 auto tx=open_pipe(peerpipe(options_,id_,L"TX"),GENERIC_WRITE);
 auto rx=open_pipe(peerpipe(options_,id_,L"RX"),GENERIC_READ);
 auto ta=open_pipe(peerpipe(options_,id_,L"TX_ACK"),GENERIC_WRITE);
 auto ra=open_pipe(peerpipe(options_,id_,L"RX_ACK"),GENERIC_READ);
 tx_=std::make_unique<Handle>(std::move(tx));rx_=std::make_unique<Handle>(std::move(rx));
 txack_=std::make_unique<Handle>(std::move(ta));rxack_=std::make_unique<Handle>(std::move(ra));
 connected_=true;rx_thread_=std::thread(&Client::receive_loop,this);ack_thread_=std::thread(&Client::ack_loop,this);
}
void Client::disconnect(){
 if(!connected_.exchange(false))return;
 for(auto*h:{tx_.get(),rx_.get(),txack_.get(),rxack_.get()})if(h&&h->valid())CancelIoEx(h->get(),nullptr);
 if(rx_thread_.joinable())rx_thread_.join();if(ack_thread_.joinable())ack_thread_.join();
 tx_.reset();rx_.reset();txack_.reset();rxack_.reset();
}
void Client::on(std::string l,Listener cb){auto id=listener_hash(l);{std::lock_guard g(label_mutex_);labels_[id]=l;}listeners_.add(std::move(l),std::move(cb));}
void Client::off(const std::string&l){listeners_.remove(l);auto id=listener_hash(l);std::lock_guard g(label_mutex_);labels_.erase(id);}
void Client::publish(const std::string&l,const Bytes&p){if(!connected_)throw std::runtime_error("disconnected");auto b=encode_frame(secrets_,next_mid_++,listener_hash(l),p);std::lock_guard g(send_mutex_);write_wire(tx_->get(),b);}
void Client::request(const std::string&,const Bytes&,std::function<void(Bytes)>){throw std::runtime_error("request/reply protocol is not implemented");}
bool Client::claim_reply(const std::string&,ReplyHandler){throw std::runtime_error("cross-process reply ownership is not implemented");}
void Client::release_reply(const std::string&){}
void Client::receive_loop(){while(connected_){try{auto b=read_frame(rx_->get(),secrets_);auto f=decode_frame(secrets_,b);std::string label;{std::lock_guard g(label_mutex_);auto i=labels_.find(f.header.listener_id);if(i!=labels_.end())label=i->second;}if(!label.empty())listeners_.dispatch(label,f.payload);auto ack=encode_ack(secrets_,f.header,'1');write_wire(txack_->get(),ack);write_wire(txack_->get(),encode_ack(secrets_,f.header,'2'));}catch(...){if(connected_)connected_=false;break;}}}
void Client::ack_loop(){while(connected_){try{std::array<std::uint8_t,101>a{};read_exact(rxack_->get(),a.data(),a.size());}catch(...){if(connected_)connected_=false;break;}}}
}


namespace pipeipc {
SharedVars::SharedVars(std::string l,std::string p,bool c):label_(std::move(l)),process_(std::move(p)),creator_(c){if(label_.empty()||process_.empty())throw std::invalid_argument("label and process id required");}
void SharedVars::write(std::string k,Value v){if(!creator_)throw std::runtime_error("only creator may mutate shared variables");if(k.empty()||k.front()=='_')throw std::invalid_argument("invalid shared variable name");std::lock_guard g(mutex_);vars_[std::move(k)]=std::move(v);}
Value SharedVars::read(const std::string&k)const{std::lock_guard g(mutex_);auto i=vars_.find(k);if(i==vars_.end())throw std::out_of_range("unknown shared variable");return i->second;}
void SharedVars::erase(const std::string&k){if(!creator_)throw std::runtime_error("only creator may mutate shared variables");std::lock_guard g(mutex_);vars_.erase(k);}
bool SharedVars::contains(const std::string&k)const{std::lock_guard g(mutex_);return vars_.contains(k);}
std::map<std::string,Value> SharedVars::snapshot()const{std::lock_guard g(mutex_);return vars_;}
void SharedVars::install_snapshot(std::map<std::string,Value>s){if(creator_)throw std::runtime_error("creator cannot install remote snapshot");std::lock_guard g(mutex_);vars_=std::move(s);}
std::map<std::string,Value> SharedVars::handle_snapshot_request()const{if(!creator_)throw std::runtime_error("only creator handles variable requests");return snapshot();}
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

