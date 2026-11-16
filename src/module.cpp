#include <pybind11/pybind11.h>
#include "quickipc.hpp"
#include <type_traits>
namespace py=pybind11; using namespace quickipc;
static Value from_py(py::handle h){
 if(h.is_none())return Value{};
 if(py::isinstance<py::bool_>(h))return Value(h.cast<bool>());
 if(py::isinstance<py::int_>(h))return Value(std::int64_t(h.cast<long long>()));
 if(py::isinstance<py::float_>(h))return Value(h.cast<double>());
 if(py::isinstance<py::str>(h))return Value(h.cast<std::string>());
 if(py::isinstance<py::bytes>(h)){auto s=h.cast<std::string>();return Value(Bytes(s.begin(),s.end()));}
 if(py::isinstance<py::dict>(h)){Dict d;for(auto item:h.cast<py::dict>()){if(!py::isinstance<py::str>(item.first))throw py::type_error("dict keys must be strings");d.values[item.first.cast<std::string>()]=from_py(item.second);}return Value(std::move(d));}
 if(py::isinstance<py::tuple>(h)){Tuple t;for(auto x:h.cast<py::tuple>())t.values.push_back(from_py(x));return Value(std::move(t));}
 if(py::isinstance<py::list>(h)){List l;for(auto x:h.cast<py::list>())l.values.push_back(from_py(x));return Value(std::move(l));}
 if(py::isinstance<py::set>(h)||py::isinstance<py::frozenset>(h)){Set s;for(auto x:h)s.values.insert(from_py(x));return Value(std::move(s));}
 throw py::type_error("unsupported value; use None/bool/int/float/str/bytes/list/tuple/set/dict");
}
static py::object to_py(const Value&v){return std::visit([](const auto&x)->py::object{using T=std::decay_t<decltype(x)>;
 if constexpr(std::is_same_v<T,std::monostate>)return py::none();
 else if constexpr(std::is_same_v<T,Dict>){py::dict d;for(auto&[k,v]:x.values)d[py::str(k)]=to_py(v);return std::move(d);}
 else if constexpr(std::is_same_v<T,List>){py::list l;for(auto&v:x.values)l.append(to_py(v));return std::move(l);}
 else if constexpr(std::is_same_v<T,Tuple>){py::tuple t(x.values.size());for(std::size_t i=0;i<x.values.size();++i)t[i]=to_py(x.values[i]);return std::move(t);}
 else if constexpr(std::is_same_v<T,Set>){py::set s;for(auto&v:x.values)s.add(to_py(v));return std::move(s);}
 else if constexpr(std::is_same_v<T,Bytes>)return py::bytes(reinterpret_cast<const char*>(x.data()),x.size());
 else return py::cast(x);
 },v.data);}
class PySharedVars {
 std::unique_ptr<SharedVars> vars_;
public:
 PySharedVars(std::string label,std::string process="python",bool creator=true):vars_(std::make_unique<SharedVars>(std::move(label),std::move(process),creator)){}
 void write(const std::string&k,py::object v){vars_->write(k,from_py(v));}
 py::object read(const std::string&k)const{return to_py(vars_->read(k));}
 void erase(const std::string&k){vars_->erase(k);}
 bool contains(const std::string&k)const{return vars_->contains(k);}
 py::dict snapshot()const{py::dict d;for(auto&[k,v]:vars_->snapshot())d[py::str(k)]=to_py(v);return d;}
};
PYBIND11_MODULE(quickipc,m){
 py::class_<PySharedVars>(m,"SharedVars")
  .def(py::init<std::string,std::string,bool>(),py::arg("label"),py::arg("process_id")="python",py::arg("creator")=true)
  .def("__getattr__",[](const PySharedVars&v,const std::string&k){try{return v.read(k);}catch(const std::out_of_range&){throw py::attribute_error(k);}})
  .def("__setattr__",[](PySharedVars&v,const std::string&k,py::object x){if(!k.empty()&&k[0]=='_')throw py::attribute_error("private attributes are reserved");v.write(k,std::move(x));})
  .def("__delattr__",[](PySharedVars&v,const std::string&k){if(!v.contains(k))throw py::attribute_error(k);v.erase(k);})
  .def("__contains__",&PySharedVars::contains)
  .def("snapshot",&PySharedVars::snapshot);
}
