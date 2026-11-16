#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <variant>
#include <vector>
namespace quickipc {
struct Value;
using Bytes = std::vector<std::uint8_t>;
using List = std::vector<Value>;
using Tuple = std::vector<Value>;
using Set = std::set<Value>;
using Dict = std::map<std::string, Value>;
struct Value {
 using Data=std::variant<std::monostate,Dict,List,Tuple,Set,std::int64_t,double,bool,Bytes>;
 Data data;
 Value()=default;
 template<class T> Value(T v):data(std::move(v)){}
 bool operator<(const Value& other) const;
};
enum class ValueTag:std::uint8_t{None=0,Dict=1,List=2,Tuple=3,Set=4,Int64=5,Float64=6,Bool=7,Bytes=8};
Bytes encode_value(const Value&);
Value decode_value(const Bytes&);
}
