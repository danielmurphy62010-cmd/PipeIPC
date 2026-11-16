#include <jni.h>
#include "quickipc/shared_vars.hpp"
#include <memory>
#include <stdexcept>
using namespace quickipc;
static std::string str(JNIEnv*e,jstring s){if(!s)throw std::invalid_argument("null string");const char*p=e->GetStringUTFChars(s,nullptr);if(!p)throw std::runtime_error("JNI string conversion failed");std::string r(p);e->ReleaseStringUTFChars(s,p);return r;}
static void fail(JNIEnv*e,const std::exception&x){jclass c=e->FindClass("java/lang/IllegalArgumentException");if(c)e->ThrowNew(c,x.what());}
static Value from_java(JNIEnv*e,jobject o){
 if(!o)return Value{};
 jclass c=e->GetObjectClass(o);
 if(e->IsInstanceOf(o,e->FindClass("java/lang/String")))return Value(str(e,(jstring)o));
 if(e->IsInstanceOf(o,e->FindClass("java/lang/Boolean"))){jmethodID m=e->GetMethodID(c,"booleanValue","()Z");return Value(bool(e->CallBooleanMethod(o,m)));}
 if(e->IsInstanceOf(o,e->FindClass("java/lang/Byte"))||e->IsInstanceOf(o,e->FindClass("java/lang/Short"))||e->IsInstanceOf(o,e->FindClass("java/lang/Integer"))||e->IsInstanceOf(o,e->FindClass("java/lang/Long"))){jmethodID m=e->GetMethodID(c,"longValue","()J");return Value(std::int64_t(e->CallLongMethod(o,m)));}
 if(e->IsInstanceOf(o,e->FindClass("java/lang/Float"))||e->IsInstanceOf(o,e->FindClass("java/lang/Double"))){jmethodID m=e->GetMethodID(c,"doubleValue","()D");return Value(double(e->CallDoubleMethod(o,m)));}
 if(e->IsInstanceOf(o,e->FindClass("[B"))){auto a=(jbyteArray)o;jsize n=e->GetArrayLength(a);Bytes b(n);e->GetByteArrayRegion(a,0,n,reinterpret_cast<jbyte*>(b.data()));return Value(std::move(b));}
 if(e->IsInstanceOf(o,e->FindClass("java/util/Map"))){Dict d;jclass mc=e->GetObjectClass(o);jmethodID ent=e->GetMethodID(mc,"entrySet","()Ljava/util/Set;");jobject set=e->CallObjectMethod(o,ent);jclass sc=e->GetObjectClass(set);jmethodID it=e->GetMethodID(sc,"iterator","()Ljava/util/Iterator;");jobject iter=e->CallObjectMethod(set,it);jclass ic=e->GetObjectClass(iter);jmethodID has=e->GetMethodID(ic,"hasNext","()Z"),next=e->GetMethodID(ic,"next","()Ljava/lang/Object;");while(e->CallBooleanMethod(iter,has)){jobject entry=e->CallObjectMethod(iter,next);jclass ec=e->GetObjectClass(entry);jmethodID getk=e->GetMethodID(ec,"getKey","()Ljava/lang/Object;"),getv=e->GetMethodID(ec,"getValue","()Ljava/lang/Object;");jobject k=e->CallObjectMethod(entry,getk),v=e->CallObjectMethod(entry,getv);if(!e->IsInstanceOf(k,e->FindClass("java/lang/String")))throw std::invalid_argument("Map keys must be strings");d.values[str(e,(jstring)k)]=from_java(e,v);e->DeleteLocalRef(entry);e->DeleteLocalRef(k);if(v)e->DeleteLocalRef(v);}return Value(std::move(d));}
 if(e->IsInstanceOf(o,e->FindClass("java/util/Set"))){Set s;jclass sc=e->GetObjectClass(o);jmethodID it=e->GetMethodID(sc,"iterator","()Ljava/util/Iterator;");jobject iter=e->CallObjectMethod(o,it);jclass ic=e->GetObjectClass(iter);jmethodID has=e->GetMethodID(ic,"hasNext","()Z"),next=e->GetMethodID(ic,"next","()Ljava/lang/Object;");while(e->CallBooleanMethod(iter,has)){jobject v=e->CallObjectMethod(iter,next);s.values.insert(from_java(e,v));if(v)e->DeleteLocalRef(v);}return Value(std::move(s));}
 if(e->IsInstanceOf(o,e->FindClass("java/util/List"))){List l;jclass lc=e->GetObjectClass(o);jmethodID size=e->GetMethodID(lc,"size","()I"),get=e->GetMethodID(lc,"get","(I)Ljava/lang/Object;");for(jint i=0,n=e->CallIntMethod(o,size);i<n;i++){jobject v=e->CallObjectMethod(o,get,i);l.values.push_back(from_java(e,v));if(v)e->DeleteLocalRef(v);}return Value(std::move(l));}
 throw std::invalid_argument("Unsupported Java value: use null, String, Boolean, integral/floating wrappers, byte[], List, Set or Map");
}
static jobject to_java(JNIEnv*e,const Value&v){return std::visit([&](const auto&x)->jobject{using T=std::decay_t<decltype(x)>;
 if constexpr(std::is_same_v<T,std::monostate>)return nullptr;
 else if constexpr(std::is_same_v<T,std::string>){jclass c=e->FindClass("java/lang/String");jmethodID m=e->GetMethodID(c,"<init>","([BLjava/lang/String;)V");jbyteArray a=e->NewByteArray((jsize)x.size());e->SetByteArrayRegion(a,0,(jsize)x.size(),reinterpret_cast<const jbyte*>(x.data()));jstring enc=e->NewStringUTF("UTF-8");return e->NewObject(c,m,a,enc);}
 else if constexpr(std::is_same_v<T,bool>){jclass c=e->FindClass("java/lang/Boolean");return e->CallStaticObjectMethod(c,e->GetStaticMethodID(c,"valueOf","(Z)Ljava/lang/Boolean;"),x);}
 else if constexpr(std::is_same_v<T,std::int64_t>){jclass c=e->FindClass("java/lang/Long");return e->CallStaticObjectMethod(c,e->GetStaticMethodID(c,"valueOf","(J)Ljava/lang/Long;"),(jlong)x);}
 else if constexpr(std::is_same_v<T,double>){jclass c=e->FindClass("java/lang/Double");return e->CallStaticObjectMethod(c,e->GetStaticMethodID(c,"valueOf","(D)Ljava/lang/Double;"),(jdouble)x);}
 else if constexpr(std::is_same_v<T,Bytes>){jbyteArray a=e->NewByteArray((jsize)x.size());e->SetByteArrayRegion(a,0,(jsize)x.size(),reinterpret_cast<const jbyte*>(x.data()));return a;}
 else if constexpr(std::is_same_v<T,List>||std::is_same_v<T,Tuple>){jclass c=e->FindClass("java/util/ArrayList");jobject a=e->NewObject(c,e->GetMethodID(c,"<init>","()V"));jmethodID add=e->GetMethodID(c,"add","(Ljava/lang/Object;)Z");for(auto&v:x.values){jobject o=to_java(e,v);e->CallBooleanMethod(a,add,o);if(o)e->DeleteLocalRef(o);}return a;}
 else if constexpr(std::is_same_v<T,Set>){jclass c=e->FindClass("java/util/LinkedHashSet");jobject a=e->NewObject(c,e->GetMethodID(c,"<init>","()V"));jmethodID add=e->GetMethodID(c,"add","(Ljava/lang/Object;)Z");for(auto&v:x.values){jobject o=to_java(e,v);e->CallBooleanMethod(a,add,o);if(o)e->DeleteLocalRef(o);}return a;}
 else {jclass c=e->FindClass("java/util/LinkedHashMap");jobject a=e->NewObject(c,e->GetMethodID(c,"<init>","()V"));jmethodID put=e->GetMethodID(c,"put","(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");for(auto&[k,v]:x.values){jstring key=e->NewStringUTF(k.c_str());jobject val=to_java(e,v);e->CallObjectMethod(a,put,key,val);e->DeleteLocalRef(key);if(val)e->DeleteLocalRef(val);}return a;}
 },v.data);}
extern "C" JNIEXPORT jlong JNICALL Java_quickipc_SharedVars_nativeCreate(JNIEnv*e,jclass,jstring l,jstring p,jboolean c){try{return reinterpret_cast<jlong>(new SharedVars(str(e,l),str(e,p),c));}catch(const std::exception&x){fail(e,x);return 0;}}
extern "C" JNIEXPORT void JNICALL Java_quickipc_SharedVars_nativeSet(JNIEnv*e,jclass,jlong h,jstring k,jobject v){try{reinterpret_cast<SharedVars*>(h)->set(str(e,k),from_java(e,v));}catch(const std::exception&x){fail(e,x);}}
extern "C" JNIEXPORT jobject JNICALL Java_quickipc_SharedVars_nativeGet(JNIEnv*e,jclass,jlong h,jstring k){try{return to_java(e,reinterpret_cast<SharedVars*>(h)->get(str(e,k)));}catch(const std::exception&x){fail(e,x);return nullptr;}}
extern "C" JNIEXPORT jobject JNICALL Java_quickipc_SharedVars_nativeSnapshot(JNIEnv*e,jclass,jlong h){try{Dict d;d.values=reinterpret_cast<SharedVars*>(h)->snapshot();return to_java(e,Value(std::move(d)));}catch(const std::exception&x){fail(e,x);return nullptr;}}
extern "C" JNIEXPORT void JNICALL Java_quickipc_SharedVars_nativeClose(JNIEnv*,jclass,jlong h){delete reinterpret_cast<SharedVars*>(h);}
