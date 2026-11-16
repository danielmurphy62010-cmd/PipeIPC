#include <jni.h>
#include "quickipc.hpp"
#include <stdexcept>
using namespace quickipc;
static std::string str(JNIEnv*e,jstring s){
 if(!s)throw std::invalid_argument("null string");
 const char*p=e->GetStringUTFChars(s,nullptr);
 if(!p)throw std::runtime_error("JNI string conversion failed");
 std::string r(p);e->ReleaseStringUTFChars(s,p);return r;
}
static void fail(JNIEnv*e,const std::exception&x){
 jclass c=e->FindClass("java/lang/IllegalArgumentException");
 if(c)e->ThrowNew(c,x.what());
}
extern "C" JNIEXPORT jlong JNICALL Java_quickipc_SharedVars_nativeCreate(JNIEnv*e,jclass,jstring l,jstring p,jboolean c){
 try{return reinterpret_cast<jlong>(new SharedVars(str(e,l),str(e,p),c));}
 catch(const std::exception&x){fail(e,x);return 0;}
}
extern "C" JNIEXPORT void JNICALL Java_quickipc_SharedVars_nativeClose(JNIEnv*,jclass,jlong h){
 delete reinterpret_cast<SharedVars*>(h);
}
