#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <variant>
#include <vector>
namespace quickipc {
struct Value;
struct List { std::vector<Value> values; bool operator<(const List&)const; };
struct Tuple { std::vector<Value> values; bool operator<(const Tuple&)const; };
struct Set { std::set<Value> values; bool operator<(const Set&)const; };
struct Dict { std::map<std::string,Value> values; bool operator<(const Dict&)const; };
using Bytes=std::vector<std::uint8_t>;
struct Value {
 using Data=std::variant<std::monostate,Dict,List,Tuple,Set,std::int64_t,double,bool,Bytes>;
 Data data;
 Value()=default;
 template<class T> Value(T v):data(std::move(v)){}
 bool operator<(const Value&)const;
};
enum class ValueTag:std::uint8_t { None=0,Dict=1,List=2,Tuple=3,Set=4,Int64=5,Float64=6,Bool=7,Bytes=8 };
Bytes encode_value(const Value&);
Value decode_value(const Bytes&);
}
