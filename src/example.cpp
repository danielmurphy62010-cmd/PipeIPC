#include "quickipc.hpp"
#include <iostream>
#include <string>

int main(int argc,char**argv){
 try{
  if(argc>1&&std::string(argv[1])=="server"){
   quickipc::Server server;server.start();
   std::cout<<"QuickIPC server running. Press Enter to stop.\n";
   std::cin.get();server.stop();
  }else{
   quickipc::Client client;
   client.on("demo",[](const std::string&,const quickipc::Bytes&b){std::cout<<"received "<<b.size()<<" bytes\n";});
   client.connect();
   const std::string s="hello from QuickIPC";
   client.publish("demo",quickipc::Bytes(s.begin(),s.end()));
   std::cout<<"Connected. Press Enter to exit.\n";std::cin.get();
  }
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}
 return 0;
}
