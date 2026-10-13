#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace assets {
using TemplateReader=std::function<std::optional<std::vector<uint8_t>>(const std::string&)>;
// Bounded client gameObjectType scalar/DERV subset. Unset 00 20 preserves
// inheritance; only 01 20 carries an explicit int32LE. Unsupported forms,
// absent parents or cycles stay unknown instead of becoming type zero.
inline std::optional<uint32_t> gameObjectType(std::string name,const TemplateReader& reader) {
    std::set<std::string> visited;
    std::function<std::optional<uint32_t>(std::string)> resolve=[&](std::string path)->std::optional<uint32_t> {
        std::replace(path.begin(),path.end(),'\\','/');
        if(!path.starts_with("object/")||path.find("..")!=std::string::npos||path.size()>512||visited.size()>=32||!visited.insert(path).second)return {};
        auto bytes=reader(path);if(!bytes||bytes->size()<12||bytes->size()>8*1024*1024)return {};
        std::optional<uint32_t> own;std::string parent;size_t chunks=0;
        auto be=[](std::span<const uint8_t> s,size_t at){return uint32_t(s[at])<<24|uint32_t(s[at+1])<<16|uint32_t(s[at+2])<<8|s[at+3];};
        std::function<void(std::span<const uint8_t>,int,bool)> scan=[&](auto data,int depth,bool derv) {
            if(depth>32)throw std::runtime_error("template depth");
            while(!data.empty()) {
                if(data.size()<8||++chunks>4096)throw std::runtime_error("template chunks");
                const auto tag=be(data,0),length=be(data,4);
                if(length>data.size()-8)throw std::runtime_error("template extent");
                const auto payload=data.subspan(8,length);data=data.subspan(8+length);
                if(tag==0x464f524d) {
                    if(payload.size()<4)throw std::runtime_error("template form");
                    scan(payload.subspan(4),depth+1,be(payload,0)==0x44455256);
                }else if(tag==0x58585858) {
                    auto end=std::find(payload.begin(),payload.end(),uint8_t(0));
                    if(end==payload.end())continue;
                    const std::string key(payload.begin(),end);const size_t offset=size_t(end-payload.begin())+1;
                    if(derv){if((!parent.empty()&&parent!=key)||offset!=payload.size())throw std::runtime_error("template parent");parent=key;}
                    else if(key=="gameObjectType") {
                        auto value=payload.subspan(offset);
                        if(value.size()<2||value[1]!=0x20)throw std::runtime_error("template integer");
                        if(value[0]==0&&value.size()==2)continue;
                        if(value[0]!=1||value.size()!=6||own)throw std::runtime_error("template integer form");
                        own=uint32_t(value[2])|uint32_t(value[3])<<8|uint32_t(value[4])<<16|uint32_t(value[5])<<24;
                    }
                }
            }
        };
        try{if(be(*bytes,0)!=0x464f524d)return {};scan(*bytes,0,false);}catch(const std::exception&){return {};}
        if(own)return own;
        return parent.empty()?std::optional<uint32_t>{}:resolve(parent);
    };
    try{return resolve(std::move(name));}catch(const std::exception&){return {};}
}
}
