#include "runtime.hpp"
#include <iostream>
int main(){try{quickipc::Client c;c.on("demo",[](const std::string&,const quickipc::Bytes&b){std::cout<<"received "<<b.size()<<" bytes\\n";});c.connect();std::string s="hello from QuickIPC";c.publish("demo",quickipc::Bytes(s.begin(),s.end()));std::cout<<"Connected; press Enter to exit.\\n";std::cin.get();}catch(const std::exception&e){std::cerr<<e.what()<<"\\n";return 1;}}
