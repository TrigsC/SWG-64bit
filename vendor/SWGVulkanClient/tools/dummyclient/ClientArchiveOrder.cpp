#include "ClientArchiveOrder.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <stdexcept>
namespace dummyclient {
std::vector<std::string> clientArchiveOrder(const std::string& root,
                                          const std::vector<std::string>& fallback) {
    std::ifstream input(std::filesystem::path(root)/"swgemu_live.cfg");
    if(!input) return fallback;
    std::regex pattern(R"(searchTree_[0-9]+_([0-9]+)\s*=\s*([^\s]+\.tre))");
    std::vector<std::pair<int,std::string>> entries;
    for(std::string line;std::getline(input,line);) {
        std::smatch match;if(std::regex_search(line,match,pattern)) entries.emplace_back(std::stoi(match[1]),match[2]);
    }
    if(entries.empty()) throw std::runtime_error("No archive searchTree entries in swgemu_live.cfg");
    std::stable_sort(entries.begin(),entries.end(),[](auto& a,auto& b){return a.first>b.first;});
    std::vector<std::string> result;std::set<std::string> seen;
    for(auto& [priority,name]:entries) if(seen.insert(name).second) {
        std::filesystem::path path(name);
        if(path.is_absolute()||name.find("..")!=std::string::npos) throw std::runtime_error("Archive must be a relative filename: "+name);
        if(!std::filesystem::is_regular_file(std::filesystem::path(root)/path)) throw std::runtime_error("Configured archive missing: "+name);
        result.push_back(name);
    }
    return result;
}
}
