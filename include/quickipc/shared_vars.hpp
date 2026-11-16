#pragma once
#include "quickipc/value.hpp"
#include <map>
#include <mutex>
#include <string>
namespace quickipc {
class SharedVars{
public:
 SharedVars(std::string listener_label,std::string process_id,bool creator);
 const std::string& label()const noexcept{return label_;}
 bool is_creator()const noexcept{return creator_;}
 void set(std::string,Value); Value get(const std::string&)const;
 std::map<std::string,Value> snapshot()const;
 void join(const std::map<std::string,Value>& creator_snapshot);
 std::map<std::string,Value> handle_variable_request()const;
private:
 std::string label_,process_id_; bool creator_;
 mutable std::mutex mutex_; std::map<std::string,Value> vars_;
};
}
