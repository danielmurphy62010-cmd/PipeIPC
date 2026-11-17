#include <jni.h>
#include "pipeipc.hpp"
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
using namespace pipeipc;

namespace {
void throw_java(JNIEnv*e,const char*type,const std::string&message){
 jclass c=e->FindClass(type);if(c)e->ThrowNew(c,message.c_str());
}
std::string from_string(JNIEnv*e,jstring value){
 if(!value)throw std::invalid_argument("null string");
 const char*p=e->GetStringUTFChars(value,nullptr);if(!p)throw std::runtime_error("JNI string conversion failed");
 std::string out(p);e->ReleaseStringUTFChars(value,p);return out;
}
Bytes from_byte_array(JNIEnv*e,jbyteArray value){
 if(!value)throw std::invalid_argument("null byte array");
 jsize n=e->GetArrayLength(value);Bytes out;out.resize(static_cast<std::size_t>(n));
 if(n)e->GetByteArrayRegion(value,0,n,reinterpret_cast<jbyte*>(out.data()));
 return out;
}
struct GlobalRef {
 JavaVM* vm{}; jobject object{};
 GlobalRef(JNIEnv*e,jobject o){if(e->GetJavaVM(&vm)!=JNI_OK)throw std::runtime_error("cannot obtain JavaVM");object=e->NewGlobalRef(o);if(!object)throw std::runtime_error("cannot retain Java callback");}
 ~GlobalRef(){
  if(!vm||!object)return;JNIEnv*e=nullptr;bool detach=false;
  jint state=vm->GetEnv(reinterpret_cast<void**>(&e),JNI_VERSION_1_6);
  if(state==JNI_EDETACHED){if(vm->AttachCurrentThread(reinterpret_cast<void**>(&e),nullptr)!=JNI_OK)return;detach=true;}
  if(e)e->DeleteGlobalRef(object);if(detach)vm->DetachCurrentThread();
 }
};
struct JavaClientHandle {
 Client client;
 explicit JavaClientHandle(std::wstring name):client([&]{Options o;o.name=std::move(name);return o;}()){}
};
using ClientPtr=std::shared_ptr<JavaClientHandle>;
ClientPtr& client_ptr(jlong h){if(!h)throw std::invalid_argument("closed Client");return *reinterpret_cast<ClientPtr*>(h);}
struct JavaServerHandle {
 Server server;
 explicit JavaServerHandle(std::wstring name):server([&]{Options o;o.name=std::move(name);return o;}()){}
};
struct JavaSharedVarsHandle {
 ClientPtr client;
 SharedVars vars;
 explicit JavaSharedVarsHandle(ClientPtr c):client(std::move(c)),vars(client->client){}
};
using ServerPtr=std::shared_ptr<JavaServerHandle>;
JavaServerHandle& server_ref(jlong h){if(!h)throw std::invalid_argument("closed Server");return **reinterpret_cast<ServerPtr*>(h);}
JavaSharedVarsHandle& vars_ref(jlong h){if(!h)throw std::invalid_argument("closed SharedVars");return *reinterpret_cast<JavaSharedVarsHandle*>(h);}

Value from_java(JNIEnv*e,jobject o){
 if(!o)return Value{};
 jclass boolean_cls=e->FindClass("java/lang/Boolean");
 if(e->IsInstanceOf(o,boolean_cls)){jmethodID m=e->GetMethodID(boolean_cls,"booleanValue","()Z");return Value(bool(e->CallBooleanMethod(o,m)));}
 jclass string_cls=e->FindClass("java/lang/String");
 if(e->IsInstanceOf(o,string_cls))return Value(from_string(e,static_cast<jstring>(o)));
 jclass bytes_cls=e->FindClass("[B");
 if(e->IsInstanceOf(o,bytes_cls))return Value(from_byte_array(e,static_cast<jbyteArray>(o)));
 jclass integer_cls=e->FindClass("java/lang/Integer");
 jclass long_cls=e->FindClass("java/lang/Long");
 jclass short_cls=e->FindClass("java/lang/Short");
 jclass byte_cls=e->FindClass("java/lang/Byte");
 jclass double_cls=e->FindClass("java/lang/Double");
 jclass float_cls=e->FindClass("java/lang/Float");
 if(e->IsInstanceOf(o,integer_cls)||e->IsInstanceOf(o,long_cls)||e->IsInstanceOf(o,short_cls)||e->IsInstanceOf(o,byte_cls)){
  jclass number=e->FindClass("java/lang/Number");jmethodID m=e->GetMethodID(number,"longValue","()J");auto v=e->CallLongMethod(o,m);return Value(std::int64_t(v));
 }
 if(e->IsInstanceOf(o,double_cls)||e->IsInstanceOf(o,float_cls)){
  jclass number=e->FindClass("java/lang/Number");jmethodID m=e->GetMethodID(number,"doubleValue","()D");auto v=e->CallDoubleMethod(o,m);return Value(double(v));
 }
 jclass list_cls=e->FindClass("java/util/List");
 jclass set_cls=e->FindClass("java/util/Set");
 jclass map_cls=e->FindClass("java/util/Map");
 if(e->IsInstanceOf(o,list_cls)||e->IsInstanceOf(o,set_cls)){
  jclass collection=e->FindClass("java/util/Collection");jmethodID to_array=e->GetMethodID(collection,"toArray","()[Ljava/lang/Object;");
  auto array=static_cast<jobjectArray>(e->CallObjectMethod(o,to_array));jsize n=e->GetArrayLength(array);
  if(e->IsInstanceOf(o,set_cls)){Set values;for(jsize i=0;i<n;i++){jobject item=e->GetObjectArrayElement(array,i);values.values.insert(from_java(e,item));e->DeleteLocalRef(item);}e->DeleteLocalRef(array);return Value(std::move(values));}
  List values;for(jsize i=0;i<n;i++){jobject item=e->GetObjectArrayElement(array,i);values.values.push_back(from_java(e,item));e->DeleteLocalRef(item);}e->DeleteLocalRef(array);return Value(std::move(values));
 }
 if(e->IsInstanceOf(o,map_cls)){
  jmethodID entry_set=e->GetMethodID(map_cls,"entrySet","()Ljava/util/Set;");jobject entries=e->CallObjectMethod(o,entry_set);
  jclass collection=e->FindClass("java/util/Collection");jmethodID to_array=e->GetMethodID(collection,"toArray","()[Ljava/lang/Object;");
  auto array=static_cast<jobjectArray>(e->CallObjectMethod(entries,to_array));jsize n=e->GetArrayLength(array);jclass entry_cls=e->FindClass("java/util/Map$Entry");
  jmethodID get_key=e->GetMethodID(entry_cls,"getKey","()Ljava/lang/Object;");jmethodID get_value=e->GetMethodID(entry_cls,"getValue","()Ljava/lang/Object;");
  Dict values;jclass string_type=e->FindClass("java/lang/String");for(jsize i=0;i<n;i++){jobject entry=e->GetObjectArrayElement(array,i);jobject key=e->CallObjectMethod(entry,get_key);jobject value=e->CallObjectMethod(entry,get_value);if(!key||!e->IsInstanceOf(key,string_type))throw std::invalid_argument("Java shared map keys must be strings");std::string k=from_string(e,static_cast<jstring>(key));values.values[k]=from_java(e,value);e->DeleteLocalRef(entry);e->DeleteLocalRef(key);if(value)e->DeleteLocalRef(value);}
  e->DeleteLocalRef(array);e->DeleteLocalRef(entries);return Value(std::move(values));
 }
 throw std::invalid_argument("unsupported Java shared value type");
}
jobject to_java(JNIEnv*e,const Value&v){
 return std::visit([&](const auto&x)->jobject{using T=std::decay_t<decltype(x)>;
  if constexpr(std::is_same_v<T,std::monostate>)return nullptr;
  else if constexpr(std::is_same_v<T,bool>){jclass c=e->FindClass("java/lang/Boolean");jmethodID m=e->GetStaticMethodID(c,"valueOf","(Z)Ljava/lang/Boolean;");return e->CallStaticObjectMethod(c,m,jboolean(x));}
  else if constexpr(std::is_same_v<T,std::int64_t>){jclass c=e->FindClass("java/lang/Long");jmethodID m=e->GetStaticMethodID(c,"valueOf","(J)Ljava/lang/Long;");return e->CallStaticObjectMethod(c,m,jlong(x));}
  else if constexpr(std::is_same_v<T,double>){jclass c=e->FindClass("java/lang/Double");jmethodID m=e->GetStaticMethodID(c,"valueOf","(D)Ljava/lang/Double;");return e->CallStaticObjectMethod(c,m,jdouble(x));}
  else if constexpr(std::is_same_v<T,std::string>)return e->NewStringUTF(x.c_str());
  else if constexpr(std::is_same_v<T,Bytes>){jbyteArray a=e->NewByteArray(jsize(x.size()));if(!x.empty())e->SetByteArrayRegion(a,0,jsize(x.size()),reinterpret_cast<const jbyte*>(x.data()));return a;}
  else if constexpr(std::is_same_v<T,Dict>){jclass c=e->FindClass("java/util/LinkedHashMap");jmethodID init=e->GetMethodID(c,"<init>","()V");jobject map=e->NewObject(c,init);jmethodID put=e->GetMethodID(c,"put","(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");for(auto&[k,val]:x.values){jstring key=e->NewStringUTF(k.c_str());jobject item=to_java(e,val);e->CallObjectMethod(map,put,key,item);e->DeleteLocalRef(key);if(item)e->DeleteLocalRef(item);}return map;}
  else if constexpr(std::is_same_v<T,List>||std::is_same_v<T,Tuple>){jclass c=e->FindClass("java/util/ArrayList");jmethodID init=e->GetMethodID(c,"<init>","()V");jobject list=e->NewObject(c,init);jmethodID add=e->GetMethodID(c,"add","(Ljava/lang/Object;)Z");for(auto&val:x.values){jobject item=to_java(e,val);e->CallBooleanMethod(list,add,item);if(item)e->DeleteLocalRef(item);}return list;}
  else if constexpr(std::is_same_v<T,Set>){jclass c=e->FindClass("java/util/LinkedHashSet");jmethodID init=e->GetMethodID(c,"<init>","()V");jobject set=e->NewObject(c,init);jmethodID add=e->GetMethodID(c,"add","(Ljava/lang/Object;)Z");for(auto&val:x.values){jobject item=to_java(e,val);e->CallBooleanMethod(set,add,item);if(item)e->DeleteLocalRef(item);}return set;}
  else return nullptr;
 },v.data);
}
void set_message_listener(JNIEnv*e,Client&client,const std::string&label,jobject listener){
 auto ref=std::make_shared<GlobalRef>(e,listener);
 jclass cls=e->GetObjectClass(listener);jmethodID method=e->GetMethodID(cls,"onMessage","(Ljava/lang/String;Ljava/lang/Object;)V");if(!method)throw std::invalid_argument("listener must implement onMessage(String, Object)");
 client.on(label,[ref,method](const std::string&name,const Value&value){
  JNIEnv*env=nullptr;bool detach=false;jint state=ref->vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6);
  if(state==JNI_EDETACHED){if(ref->vm->AttachCurrentThread(reinterpret_cast<void**>(&env),nullptr)!=JNI_OK)return;detach=true;}
  jstring jname=env->NewStringUTF(name.c_str());jobject payload=to_java(env,value);
  env->CallVoidMethod(ref->object,method,jname,payload);
  if(env->ExceptionCheck())env->ExceptionClear();
  env->DeleteLocalRef(jname);if(payload)env->DeleteLocalRef(payload);if(detach)ref->vm->DetachCurrentThread();
 });
}
}

extern "C" JNIEXPORT jlong JNICALL Java_pipeipc_Server_nativeCreate(JNIEnv*e,jclass,jstring name){
 try{auto n=from_string(e,name);return reinterpret_cast<jlong>(new ServerPtr(std::make_shared<JavaServerHandle>(std::wstring(n.begin(),n.end()))));}
 catch(const std::exception&x){throw_java(e,"java/lang/IllegalStateException",x.what());return 0;}
}
extern "C" JNIEXPORT void JNICALL Java_pipeipc_Server_nativeStart(JNIEnv*e,jclass,jlong h){try{server_ref(h).server.start();}catch(const std::exception&x){throw_java(e,"java/lang/IllegalStateException",x.what());}}
extern "C" JNIEXPORT void JNICALL Java_pipeipc_Server_nativeStop(JNIEnv*e,jclass,jlong h){try{server_ref(h).server.stop();}catch(const std::exception&x){throw_java(e,"java/lang/IllegalStateException",x.what());}}
extern "C" JNIEXPORT void JNICALL Java_pipeipc_Server_nativeSend(JNIEnv*e,jclass,jlong h,jstring label,jobject value){try{server_ref(h).server.send(from_string(e,label),from_java(e,value));}catch(const std::exception&x){throw_java(e,"java/lang/IllegalStateException",x.what());}}
extern "C" JNIEXPORT void JNICALL Java_pipeipc_Server_nativeClose(JNIEnv*,jclass,jlong h){delete reinterpret_cast<ServerPtr*>(h);}

extern "C" JNIEXPORT jlong JNICALL Java_pipeipc_Client_nativeCreate(JNIEnv*e,jclass,jstring name){
 try{auto n=from_string(e,name);return reinterpret_cast<jlong>(new ClientPtr(std::make_shared<JavaClientHandle>(std::wstring(n.begin(),n.end()))));}
 catch(const std::exception&x){throw_java(e,"java/lang/IllegalStateException",x.what());return 0;}
}
extern "C" JNIEXPORT void JNICALL Java_pipeipc_Client_nativeConnect(JNIEnv*e,jclass,jlong h){try{client_ptr(h)->client.connect();}catch(const std::exception&x){throw_java(e,"java/lang/IllegalStateException",x.what());}}
extern "C" JNIEXPORT void JNICALL Java_pipeipc_Client_nativeDisconnect(JNIEnv*e,jclass,jlong h){try{client_ptr(h)->client.disconnect();}catch(const std::exception&x){throw_java(e,"java/lang/IllegalStateException",x.what());}}
extern "C" JNIEXPORT jboolean JNICALL Java_pipeipc_Client_nativeConnected(JNIEnv*e,jclass,jlong h){try{return client_ptr(h)->client.connected()?JNI_TRUE:JNI_FALSE;}catch(const std::exception&x){throw_java(e,"java/lang/IllegalStateException",x.what());return JNI_FALSE;}}
extern "C" JNIEXPORT void JNICALL Java_pipeipc_Client_nativeSend(JNIEnv*e,jclass,jlong h,jstring label,jobject value){try{client_ptr(h)->client.send(from_string(e,label),from_java(e,value));}catch(const std::exception&x){throw_java(e,"java/lang/IllegalStateException",x.what());}}
extern "C" JNIEXPORT jobject JNICALL Java_pipeipc_Client_nativeRecv(JNIEnv*e,jclass,jlong h,jlong timeout){
 try{
  auto message=timeout<0?client_ptr(h)->client.recv():client_ptr(h)->client.recv(std::chrono::milliseconds(timeout));
  if(!message)return nullptr;
  jclass type=e->FindClass("pipeipc/Message");jmethodID ctor=e->GetMethodID(type,"<init>","(Ljava/lang/String;Ljava/lang/Object;)V");
  jstring label=e->NewStringUTF(message->label.c_str());jobject value=to_java(e,message->value);jobject result=e->NewObject(type,ctor,label,value);
  e->DeleteLocalRef(label);if(value)e->DeleteLocalRef(value);return result;
 }catch(const std::exception&x){throw_java(e,"java/lang/IllegalStateException",x.what());return nullptr;}
}
extern "C" JNIEXPORT void JNICALL Java_pipeipc_Client_nativeOn(JNIEnv*e,jclass,jlong h,jstring label,jobject callback){try{set_message_listener(e,client_ptr(h)->client,from_string(e,label),callback);}catch(const std::exception&x){throw_java(e,"java/lang/IllegalStateException",x.what());}}
extern "C" JNIEXPORT void JNICALL Java_pipeipc_Client_nativeOff(JNIEnv*e,jclass,jlong h,jstring label){try{client_ptr(h)->client.off(from_string(e,label));}catch(const std::exception&x){throw_java(e,"java/lang/IllegalStateException",x.what());}}
extern "C" JNIEXPORT void JNICALL Java_pipeipc_Client_nativeClose(JNIEnv*,jclass,jlong h){delete reinterpret_cast<ClientPtr*>(h);}

extern "C" JNIEXPORT jlong JNICALL Java_pipeipc_SharedVars_nativeCreate(JNIEnv*e,jclass,jlong client){
 try{return reinterpret_cast<jlong>(new JavaSharedVarsHandle(client_ptr(client)));}
 catch(const std::exception&x){throw_java(e,"java/lang/IllegalStateException",x.what());return 0;}
}
extern "C" JNIEXPORT void JNICALL Java_pipeipc_SharedVars_nativeWrite(JNIEnv*e,jclass,jlong h,jstring key,jobject value){try{vars_ref(h).vars.write(from_string(e,key),from_java(e,value));}catch(const std::exception&x){throw_java(e,"java/lang/IllegalArgumentException",x.what());}}
extern "C" JNIEXPORT jobject JNICALL Java_pipeipc_SharedVars_nativeRead(JNIEnv*e,jclass,jlong h,jstring key){try{return to_java(e,vars_ref(h).vars.read(from_string(e,key)));}catch(const std::exception&x){throw_java(e,"java/lang/IllegalArgumentException",x.what());return nullptr;}}
extern "C" JNIEXPORT void JNICALL Java_pipeipc_SharedVars_nativeDelete(JNIEnv*e,jclass,jlong h,jstring key){try{vars_ref(h).vars.erase(from_string(e,key));}catch(const std::exception&x){throw_java(e,"java/lang/IllegalArgumentException",x.what());}}
extern "C" JNIEXPORT jboolean JNICALL Java_pipeipc_SharedVars_nativeContains(JNIEnv*e,jclass,jlong h,jstring key){try{return vars_ref(h).vars.contains(from_string(e,key))?JNI_TRUE:JNI_FALSE;}catch(const std::exception&x){throw_java(e,"java/lang/IllegalStateException",x.what());return JNI_FALSE;}}
extern "C" JNIEXPORT jobject JNICALL Java_pipeipc_SharedVars_nativeSnapshot(JNIEnv*e,jclass,jlong h){
 try{Dict values;for(auto&[k,v]:vars_ref(h).vars.snapshot())values.values.emplace(k,v);return to_java(e,Value(std::move(values)));}
 catch(const std::exception&x){throw_java(e,"java/lang/IllegalStateException",x.what());return nullptr;}
}
extern "C" JNIEXPORT jlong JNICALL Java_pipeipc_SharedVars_nativeRevision(JNIEnv*e,jclass,jlong h){try{return jlong(vars_ref(h).vars.revision());}catch(const std::exception&x){throw_java(e,"java/lang/IllegalStateException",x.what());return 0;}}
extern "C" JNIEXPORT void JNICALL Java_pipeipc_SharedVars_nativeClose(JNIEnv*,jclass,jlong h){delete reinterpret_cast<JavaSharedVarsHandle*>(h);}
