#include "quickipc/pipe.hpp"
#include <stdexcept>
#include <utility>
namespace quickipc {
Handle::~Handle(){if(valid())CloseHandle(h_);}
Handle::Handle(Handle&&o)noexcept:h_(std::exchange(o.h_,INVALID_HANDLE_VALUE)){}
Handle&Handle::operator=(Handle&&o)noexcept{if(this!=&o){if(valid())CloseHandle(h_);h_=std::exchange(o.h_,INVALID_HANDLE_VALUE);}return *this;}
HANDLE Handle::release()noexcept{return std::exchange(h_,INVALID_HANDLE_VALUE);}
void write_exact(HANDLE h,const void*p,std::size_t n){auto b=static_cast<const std::uint8_t*>(p);while(n){DWORD done=0,part=DWORD((n>MAXDWORD)?MAXDWORD:n);if(!WriteFile(h,b,part,&done,nullptr)||!done)throw std::runtime_error("named pipe write failed");b+=done;n-=done;}}
void read_exact(HANDLE h,void*p,std::size_t n){auto b=static_cast<std::uint8_t*>(p);while(n){DWORD done=0,part=DWORD((n>MAXDWORD)?MAXDWORD:n);if(!ReadFile(h,b,part,&done,nullptr)||!done)throw std::runtime_error("named pipe read failed");b+=done;n-=done;}}
Handle create_pipe(const std::wstring&name,DWORD mode){HANDLE h=CreateNamedPipeW(name.c_str(),mode,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,1u<<20,1u<<20,0,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("CreateNamedPipeW failed");return Handle(h);}
Handle open_pipe(const std::wstring&name,DWORD access,DWORD timeout){if(!WaitNamedPipeW(name.c_str(),timeout))throw std::runtime_error("WaitNamedPipeW failed");HANDLE h=CreateFileW(name.c_str(),access,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("CreateFileW failed");return Handle(h);}
}
