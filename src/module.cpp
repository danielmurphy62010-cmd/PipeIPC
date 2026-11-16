#include <pybind11/pybind11.h>
#include "pipeipc.hpp"
#include <Python.h>
#include <type_traits>
#include <memory>
#include <stdexcept>
namespace py=pybind11; using namespace pipeipc;

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
static Bytes from_bytes(py::bytes value){auto s=value.cast<std::string>();return Bytes(s.begin(),s.end());}
class PyServer {
 std::unique_ptr<Server> server_;
public:
 explicit PyServer(std::wstring name){Options o;o.name=std::move(name);server_=std::make_unique<Server>(std::move(o));}
 ~PyServer(){if(server_)server_->stop();}
 void start(){server_->start();}
 void stop(){server_->stop();}
 bool running()const{return server_->running();}
 void publish(const std::string&label,py::bytes data){server_->publish(label,from_bytes(std::move(data)));}
 void send(const std::string&label,py::object value){server_->send(label,from_py(value));}
};
class PyClient {
 Client client_;
public:
 explicit PyClient(std::wstring name):client_([&]{Options o;o.name=std::move(name);return o;}()){}
 ~PyClient(){py::gil_scoped_release release;client_.disconnect();}
 Client& core(){return client_;}
 void connect(){client_.connect();}
 void disconnect(){py::gil_scoped_release release;client_.disconnect();}
 bool connected()const{return client_.connected();}
 std::string id()const{return client_.id();}
 void publish(const std::string&label,py::bytes data){client_.publish(label,from_bytes(std::move(data)));}
 void send(const std::string&label,py::object value){client_.send(label,from_py(value));}
 void on(const std::string&label,py::function cb,bool decode){
  auto owned=std::shared_ptr<py::function>(new py::function(std::move(cb)),[](py::function*p){
   if(Py_IsInitialized()){py::gil_scoped_acquire gil;delete p;}else delete p;
  });
  client_.on(label,[owned,decode](const std::string&name,const Bytes&payload){
   py::gil_scoped_acquire gil;
   py::object message;
   if(decode)message=to_py(decode_value(payload));
   else message=py::bytes(reinterpret_cast<const char*>(payload.data()),payload.size());
   (*owned)(name,std::move(message));
  });
 }
 void off(const std::string&label){client_.off(label);}
};
class PySharedVars {
 std::unique_ptr<SharedVars> vars_;
public:
 explicit PySharedVars(PyClient&client):vars_(std::make_unique<SharedVars>(client.core())){}
 void close(){vars_.reset();}
 void write(const std::string&k,py::object v){if(!vars_)throw std::runtime_error("SharedVars is closed");vars_->write(k,from_py(v));}
 py::object read(const std::string&k)const{if(!vars_)throw std::runtime_error("SharedVars is closed");return to_py(vars_->read(k));}
 void erase(const std::string&k){if(!vars_)throw std::runtime_error("SharedVars is closed");vars_->erase(k);}
 bool contains(const std::string&k)const{return vars_&&vars_->contains(k);}
 bool ready()const{return vars_&&vars_->ready();}
 std::uint64_t revision()const{return vars_?vars_->revision():0;}
 py::dict snapshot()const{if(!vars_)throw std::runtime_error("SharedVars is closed");py::dict d;for(auto&[k,v]:vars_->snapshot())d[py::str(k)]=to_py(v);return d;}
};
PYBIND11_MODULE(pipeipc,m){
 m.doc()="PipeIPC named-pipe messaging and brokered shared variables";
 py::class_<PyServer>(m,"Server")
  .def(py::init<std::wstring>(),py::arg("name")=L"default")
  .def("start",&PyServer::start)
  .def("stop",&PyServer::stop,py::call_guard<py::gil_scoped_release>())
  .def_property_readonly("running",&PyServer::running)
  .def("publish",&PyServer::publish,py::arg("label"),py::arg("payload"))
  .def("send",&PyServer::send,py::arg("label"),py::arg("value"));
 py::class_<PyClient>(m,"Client")
  .def(py::init<std::wstring>(),py::arg("name")=L"default")
  .def("connect",&PyClient::connect,py::call_guard<py::gil_scoped_release>())
  .def("disconnect",&PyClient::disconnect)
  .def_property_readonly("connected",&PyClient::connected)
  .def_property_readonly("id",&PyClient::id)
  .def("publish",&PyClient::publish,py::arg("label"),py::arg("payload"))
  .def("send",&PyClient::send,py::arg("label"),py::arg("value"))
  .def("on",[](PyClient&c,const std::string&label,py::function cb){c.on(label,std::move(cb),false);},py::arg("label"),py::arg("callback"))
  .def("on_value",[](PyClient&c,const std::string&label,py::function cb){c.on(label,std::move(cb),true);},py::arg("label"),py::arg("callback"))
  .def("off",&PyClient::off,py::arg("label"));
 py::class_<PySharedVars>(m,"SharedVars")
  .def(py::init([](PyClient&c){py::gil_scoped_release release;return std::make_unique<PySharedVars>(c);}),py::keep_alive<1,2>())
  .def("__getattr__",[](const PySharedVars&v,const std::string&k){try{return v.read(k);}catch(const std::out_of_range&){throw py::attribute_error(k);}})
  .def("__setattr__",[](PySharedVars&v,const std::string&k,py::object x){if(k.empty())throw py::attribute_error("empty variable name");v.write(k,std::move(x));})
  .def("__delattr__",[](PySharedVars&v,const std::string&k){if(!v.contains(k))throw py::attribute_error(k);v.erase(k);})
  .def("__contains__",&PySharedVars::contains)
  .def_property_readonly("ready",&PySharedVars::ready)
  .def_property_readonly("revision",&PySharedVars::revision)
  .def("get",&PySharedVars::read,py::arg("key"))
  .def("set",&PySharedVars::write,py::arg("key"),py::arg("value"))
  .def("delete",&PySharedVars::erase,py::arg("key"))
  .def("snapshot",&PySharedVars::snapshot)
  .def("close",&PySharedVars::close);
}
