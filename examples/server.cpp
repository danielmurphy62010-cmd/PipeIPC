#include "quickipc/runtime.hpp"
#include <iostream>
int main(){try{quickipc::Server server;server.start();std::cout<<"QuickIPC server running. Press Enter to stop.\\n";std::cin.get();server.stop();}catch(const std::exception&e){std::cerr<<e.what()<<"\\n";return 1;}}
