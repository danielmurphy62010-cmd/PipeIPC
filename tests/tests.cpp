#include "quickipc/value.hpp"
#include "quickipc/routing.hpp"
#include <cassert>
using namespace quickipc;
int main(){
 Value l(List{{Value(std::int64_t(1))}}),t(Tuple{{Value(std::int64_t(1))}});
 auto dl=decode_value(encode_value(l)),dt=decode_value(encode_value(t));
 assert(dl.data.index()==2&&dt.data.index()==3);
 Value s(Set{{Value(std::int64_t(4)),Value(std::int64_t(5))}});
 assert(decode_value(encode_value(s)).data.index()==4);
 assert(decode_value(encode_value(Value(std::int64_t(-5)))).data.index()==5);
 assert(decode_value(encode_value(Value(3.25))).data.index()==6);
 assert(decode_value(encode_value(Value(true))).data.index()==7);
 assert(decode_value(encode_value(Value(Bytes{0,1,255}))).data.index()==8);
 assert(decode_value(encode_value(Value{})).data.index()==0);
 ListenerRegistry r;int a=0,b=0,c=0;
 r.add("abc",[&](auto&,auto&){++a;});r.add("abc.def",[&](auto&,auto&){++b;});r.add("abc.def.jkl",[&](auto&,auto&){++c;});
 r.dispatch("abc.def.ghi",{});assert(a==1&&b==1&&c==0);
 return 0;
}
