#include "quickipc/protocol.hpp"
#include <blake3.h>
#include <algorithm>
#include <stdexcept>
namespace quickipc{namespace{
Digest hash(std::span<const std::uint8_t>b){Digest d{};blake3_hasher h;blake3_hasher_init(&h);blake3_hasher_update(&h,b.data(),b.size());blake3_hasher_finalize(&h,d.data(),d.size());return d;}
void add(Bytes&b,const void*p,std::size_t n){auto q=static_cast<const std::uint8_t*>(p);b.insert(b.end(),q,q+n);}
void w32(Bytes&b,std::uint32_t v){for(int i=0;i<4;i++)b.push_back(std::uint8_t(v>>(i*8)));}
void w64(Bytes&b,std::uint64_t v){for(int i=0;i<8;i++)b.push_back(std::uint8_t(v>>(i*8)));}
std::uint32_t r32(const std::uint8_t*p){std::uint32_t v=0;for(int i=0;i<4;i++)v|=std::uint32_t(p[i])<<(i*8);return v;}
std::uint64_t r64(const std::uint8_t*p){std::uint64_t v=0;for(int i=0;i<8;i++)v|=std::uint64_t(p[i])<<(i*8);return v;}
}
Bytes encode_frame(const ProtocolSecrets&s,std::uint64_t id,std::span<const std::uint8_t>lid,std::span<const std::uint8_t>p){
 if(p.size()>max_payload_size||lid.size()!=8)throw std::runtime_error("invalid frame input");Bytes h;w32(h,std::uint32_t(p.size()));w64(h,id);auto hh=hash(h),ph=hash(p);Bytes o;add(o,s.magic1.data(),8);add(o,h.data(),h.size());add(o,hh.data(),32);add(o,ph.data(),32);add(o,lid.data(),8);add(o,p.data(),p.size());add(o,s.magic2.data(),8);return o;}
Frame decode_frame(const ProtocolSecrets&s,std::span<const std::uint8_t>w){
 constexpr std::size_t fixed=100;if(w.size()<fixed||!std::equal(s.magic1.begin(),s.magic1.end(),w.begin()))throw std::runtime_error("bad leading magic");auto n=r32(w.data()+8);if(n>max_payload_size||w.size()!=fixed+n)throw std::runtime_error("bad frame length");if(!std::equal(s.magic2.begin(),s.magic2.end(),w.end()-8))throw std::runtime_error("bad trailing magic");
 Frame f;f.header.payload_size=n;f.header.message_id=r64(w.data()+12);std::copy_n(w.data()+20,32,f.header.header_hash.begin());std::copy_n(w.data()+52,32,f.header.payload_hash.begin());std::copy_n(w.data()+84,8,f.header.listener_id.begin());Bytes h;w32(h,n);w64(h,f.header.message_id);if(hash(h)!=f.header.header_hash)throw std::runtime_error("bad header digest");f.payload.assign(w.begin()+92,w.end()-8);if(hash(f.payload)!=f.header.payload_hash)throw std::runtime_error("bad payload digest");return f;}
Bytes encode_ack(const ProtocolSecrets&s,const FrameHeader&h,char status){if(status!='0'&&status!='1'&&status!='2')throw std::runtime_error("bad status");Bytes o;add(o,s.magic1.data(),8);w32(o,h.payload_size);w64(o,h.message_id);add(o,h.header_hash.data(),32);add(o,h.payload_hash.data(),32);add(o,h.listener_id.data(),8);o.push_back(std::uint8_t(status));add(o,s.magic2.data(),8);return o;}
}
