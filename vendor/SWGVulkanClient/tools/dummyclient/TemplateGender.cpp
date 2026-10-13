#include "TemplateGender.h"
#include <algorithm>
#include <bit>
#include <functional>
#include <span>
#include <stdexcept>

namespace dummyclient {
namespace {
bool validPath(const std::string& p){
 if(p.size()>1024||!p.starts_with("object/")||!p.ends_with(".iff")||p.find('\\')!=p.npos||p.find("//")!=p.npos)return false;
 size_t at=0;while(at<p.size()){auto end=p.find('/',at);if(end==p.npos)end=p.size();auto part=p.substr(at,end-at);if(part=="."||part==".."||part.empty())return false;at=end+1;}
 return std::all_of(p.begin(),p.end(),[](unsigned char c){return c>=32&&c<127;});
}
uint32_t be(std::span<const uint8_t> b,size_t at){if(at+4>b.size())throw std::runtime_error("Truncated gender IFF");return uint32_t(b[at])<<24|uint32_t(b[at+1])<<16|uint32_t(b[at+2])<<8|b[at+3];}
}
TemplateGender::Record TemplateGender::decode(const std::vector<uint8_t>& bytes){
 auto b=std::span<const uint8_t>(bytes);if(b.size()<12||be(b,0)!=0x464f524d||be(b,4)!=b.size()-8)throw std::runtime_error("Invalid gender IFF root");
 Record out;out.valid=true;out.creature=be(b,8)==0x53434f54;size_t nodes=0;bool genderSeen=false;
 std::function<void(size_t,size_t,uint32_t,size_t)> walk=[&](size_t at,size_t end,uint32_t parent,size_t depth){
  if(depth>32)throw std::runtime_error("Gender IFF depth bound");
  while(at<end){if(++nodes>4096||end-at<8)throw std::runtime_error("Gender IFF node bound");auto tag=be(b,at),length=be(b,at+4);at+=8;if(length>end-at)throw std::runtime_error("Gender IFF overrun");
   if(tag==0x464f524d){if(length<4)throw std::runtime_error("Truncated gender FORM");walk(at+4,at+length,be(b,at),depth+1);}
   else if(tag==0x58585858){auto value=b.subspan(at,length);auto zero=std::find(value.begin(),value.end(),0);
    if(parent==0x44455256){if(zero==value.end()||zero+1!=value.end())throw std::runtime_error("Invalid gender parent");std::string p(value.begin(),zero);if(!validPath(p)||(!out.parent.empty()&&out.parent!=p))throw std::runtime_error("Invalid gender derivation");out.parent=std::move(p);}
    else if(out.creature&&depth==2&&((parent>>24)&255)>='0'&&((parent>>24)&255)<='9'&&((parent>>16)&255)>='0'&&((parent>>16)&255)<='9'&&((parent>>8)&255)>='0'&&((parent>>8)&255)<='9'&&(parent&255)>='0'&&(parent&255)<='9'&&zero!=value.end()&&std::string(value.begin(),zero)=="gender"){
     if(genderSeen)throw std::runtime_error("Duplicate gender parameter");genderSeen=true;auto offset=size_t(zero-value.begin())+1;auto parameter=value.subspan(offset);
     if(parameter.size()<2)throw std::runtime_error("Truncated gender parameter");
     if(parameter[0]==0){if(parameter.size()!=2)throw std::runtime_error("Invalid inherited gender");}
     else {out.genderLoaded=true;if(parameter[0]==1){if(parameter.size()!=6)throw std::runtime_error("Invalid single gender");uint32_t raw=uint32_t(parameter[2])|uint32_t(parameter[3])<<8|uint32_t(parameter[4])<<16|uint32_t(parameter[5])<<24;out.gender=std::bit_cast<int32_t>(raw);out.genderSupported=true;}}
    }
   }
   at+=length;
  }
 };
 walk(12,b.size(),be(b,8),1);return out;
}
const TemplateGender::Record* TemplateGender::load(const std::string& path){
 if(!validPath(path))return nullptr;auto found=records_.find(path);
 if(found==records_.end()){
  if(passLoads_>=4)return nullptr;++passLoads_;++loads_;Record record;
  try{auto bytes=reader_(path);if(bytes&&bytes->size()<=256*1024)record=decode(*bytes);}catch(const std::exception&){record={};}
  if(records_.size()==128){records_.erase(recent_.front());recent_.pop_front();}
  found=records_.emplace(path,std::move(record)).first;
 }else recent_.remove(path);
 recent_.push_back(path);return &found->second;
}
std::optional<int32_t> TemplateGender::inherited(const std::string& path,std::vector<std::string>& seen){
 if(!validPath(path)||seen.size()>=32||std::find(seen.begin(),seen.end(),path)!=seen.end())return {};
 auto pointer=load(path);if(!pointer)return {};const auto record=*pointer;
 if(!record.valid||!record.creature)return {}; // Loaded child with unavailable class-compatible parent remains unknown.
 if(record.genderLoaded)return record.genderSupported?std::optional(record.gender):std::nullopt;
 if(record.parent.empty())return {}; // Optional root defaults/weighted parameters remain unverified.
 seen.push_back(path);auto value=inherited(record.parent,seen);seen.pop_back();return value;
}
std::optional<int32_t> TemplateGender::effective(const std::string& base,const std::string& alternate){
 std::string selected=base;
 if(!alternate.empty()){
  if(!validPath(alternate))return {};auto candidate=load(alternate);if(!candidate)return {};
  if(candidate->valid&&candidate->creature)selected=alternate; // Invalid/missing alternate leaves original constructor template active.
 }
 auto chosen=load(selected);if(!chosen)return {};if(!chosen->valid||!chosen->creature)return 2;
 std::vector<std::string> seen;return inherited(selected,seen);
}
}
