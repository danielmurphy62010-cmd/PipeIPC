#include "quickipc/value.hpp"
#include "quickipc/protocol.hpp"
#include <bit>
#include <stdexcept>
#include <type_traits>
namespace quickipc { namespace {
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
