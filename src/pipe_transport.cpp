#include "quickipc/pipe_transport.hpp"
#include <stdexcept>
#include <utility>
namespace quickipc{
std::wstring pipe_path(const std::wstring&s,const std::wstring&p,Channel c){const wchar_t*n=L"TX";switch(c){case Channel::TX:n=L"TX";break;case Channel::RX:n=L"RX";break;case Channel::TX_ACK:n=L"TX_ACK";break;case Channel::RX_ACK:n=L"RX_ACK";break;}return L"\\\\.\\pipe\\QuickIPC."+s+L"."+p+L"."+n;}
PipeConnection::~PipeConnection(){if(valid())CloseHandle(handle_);}
PipeConnection::PipeConnection(PipeConnection&&o)noexcept:handle_(std::exchange(o.handle_,INVALID_HANDLE_VALUE)){}
PipeConnection&PipeConnection::operator=(PipeConnection&&o)noexcept{if(this!=&o){if(valid())CloseHandle(handle_);handle_=std::exchange(o.handle_,INVALID_HANDLE_VALUE);}return *this;}
void PipeConnection::write_all(const void*p,std::size_t n){auto b=static_cast<const std::uint8_t*>(p);while(n){DWORD done=0,part=DWORD((n>0x7fffffff)?0x7fffffff:n);if(!WriteFile(handle_,b,part,&done,nullptr)||!done)throw std::runtime_error("pipe write failed");b+=done;n-=done;}}
void PipeConnection::read_all(void*p,std::size_t n){auto b=static_cast<std::uint8_t*>(p);while(n){DWORD done=0,part=DWORD((n>0x7fffffff)?0x7fffffff:n);if(!ReadFile(handle_,b,part,&done,nullptr)||!done)throw std::runtime_error("pipe read failed");b+=done;n-=done;}}
}
