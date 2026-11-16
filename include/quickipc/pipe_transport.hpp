#pragma once
#include <Windows.h>
#include <cstddef>
#include <string>
namespace quickipc {
enum class Channel{TX,RX,TX_ACK,RX_ACK};
std::wstring pipe_path(const std::wstring&,const std::wstring&,Channel);
class PipeConnection{
public:
 explicit PipeConnection(HANDLE h=INVALID_HANDLE_VALUE):handle_(h){}
 ~PipeConnection(); PipeConnection(const PipeConnection&)=delete; PipeConnection& operator=(const PipeConnection&)=delete;
 PipeConnection(PipeConnection&&)noexcept; PipeConnection& operator=(PipeConnection&&)noexcept;
 void write_all(const void*,std::size_t); void read_all(void*,std::size_t);
 bool valid()const noexcept{return handle_!=INVALID_HANDLE_VALUE;}
private: HANDLE handle_;
};
}
