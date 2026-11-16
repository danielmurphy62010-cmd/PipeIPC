#include "quickipc/runtime.hpp"
#include "quickipc/pipe.hpp"
#include <Windows.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <stdexcept>
namespace quickipc {
namespace {
std::wstring base(const Options&o){return L"\\\\.\\pipe\\QuickIPC."+o.name;}
std::wstring ctl(const Options&o){return base(o)+L".control";}
std::wstring peerpipe(const Options&o,const std::string&id,const wchar_t*ch){return base(o)+L"."+std::wstring(id.begin(),id.end())+L"."+ch;}
std::string make_id(){GUID g{};if(CoCreateGuid(&g)!=S_OK)throw std::runtime_error("CoCreateGuid failed");wchar_t b[40]{};StringFromGUID2(g,b,40);std::wstring w(b);return std::string(w.begin(),w.end());}
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
