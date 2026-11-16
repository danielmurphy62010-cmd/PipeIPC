#pragma once
#include <Windows.h>
#include <cstddef>
#include <string>
namespace quickipc {
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
}
