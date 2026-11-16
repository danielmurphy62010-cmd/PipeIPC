#include "quickipc/routing.hpp"
#include "quickipc/shared_vars.hpp"
#include <iostream>
int main(){quickipc::ListenerRegistry r;r.add("abc",[](auto&l,auto&){std::cout<<"abc: "<<l<<"\\n";});r.add("abc.def",[](auto&l,auto&){std::cout<<"abc.def: "<<l<<"\\n";});r.add("abc.def.jkl",[](auto&,auto&){std::cout<<"should not run\\n";});r.dispatch("abc.def.ghi",{});quickipc::SharedVars s("state.main","proc-1",true);s.set("count",std::int64_t{42});s.set("tuple",quickipc::Value(quickipc::Tuple{quickipc::Value(std::int64_t{1})}));}
