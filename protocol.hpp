#pragma once
#include "value.hpp"
#include <array>
#include <cstdint>
#include <span>
namespace quickipc {
inline constexpr std::uint32_t max_payload_size=16u*1024u*1024u;
using Magic=std::array<std::uint8_t,8>; using Digest=std::array<std::uint8_t,32>;
struct FrameHeader { std::uint32_t payload_size{}; std::uint64_t message_id{}; Digest header_hash{},payload_hash{}; std::array<std::uint8_t,8> listener_id{}; };
struct Frame { FrameHeader header; Bytes payload; };
struct ProtocolSecrets { Magic magic1{},magic2{}; };
ProtocolSecrets random_secrets();
std::array<std::uint8_t,8> listener_hash(const std::string&);
Bytes encode_frame(const ProtocolSecrets&,std::uint64_t,const std::array<std::uint8_t,8>&,std::span<const std::uint8_t>);
Frame decode_frame(const ProtocolSecrets&,std::span<const std::uint8_t>);
Bytes encode_ack(const ProtocolSecrets&,const FrameHeader&,char);
}
