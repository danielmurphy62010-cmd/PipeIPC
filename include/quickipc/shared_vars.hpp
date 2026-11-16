#pragma once
#include "quickipc/value.hpp"
#include <map>
#include <mutex>
#include <string>
namespace quickipc {
class SharedVars {
public:
 SharedVars(std::string label,std::string process_id,bool creator);
 const std::string& label()const noexcept{return label_;}
 bool creator()const noexcept{return creator_;}
 void write(std::string,Value);
 Value read(const std::string&)const;
 void erase(const std::string&);
 bool contains(const std::string&)const;
 std::map<std::string,Value> snapshot()const;
 void install_snapshot(std::map<std::string,Value>);
 std::map<std::string,Value> handle_snapshot_request()const;
private:
 std::string label_,process_; bool creator_;
 mutable std::mutex mutex_; std::map<std::string,Value> vars_;
};
}
