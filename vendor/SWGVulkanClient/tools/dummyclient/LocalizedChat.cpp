#include "LocalizedChat.h"
#include <bit>
#include <charconv>
#include <cmath>
#include <exception>
#include <set>
#include <algorithm>

namespace dummyclient {
namespace {
constexpr size_t maxTables=16,maxTableBytes=2*1024*1024,maxCacheBytes=8*1024*1024,maxOutput=8192;
std::u16string unicode(const std::string& text){return {text.begin(),text.end()};}
std::optional<std::u16string> finiteFloat(float value){
    if(!std::isfinite(value))return {};
    char buffer[64];auto [end,error]=std::to_chars(buffer,buffer+sizeof(buffer),value,std::chars_format::general);
    if(error!=std::errc{})return {};
    return unicode({buffer,end});
}
}
void LocalizedChat::retainSequences(std::span<const uint64_t> sequences){
    std::set<uint64_t> active(sequences.begin(),sequences.end());std::erase_if(resolved_,[&](const auto& row){return !active.contains(row.first);});
}
std::optional<std::u16string> LocalizedChat::formatRetained(uint64_t sequence,const swgproto::StringIdChatParameter& p,const ObjectName& names){
    return formatRetainedWithContext(sequence,p,[&](uint64_t id)->std::optional<ObjectInfo>{auto name=names(id);if(!name)return {};return ObjectInfo{*name};});
}
std::optional<std::u16string> LocalizedChat::formatRetainedWithContext(uint64_t sequence,const swgproto::StringIdChatParameter& p,const ObjectContext& context){
    if(auto found=resolved_.find(sequence);found!=resolved_.end())return found->second;
    auto text=formatWithContext(p,context);if(text){if(resolved_.size()==1280)resolved_.erase(resolved_.begin());resolved_.emplace(sequence,*text);}return text;
}
LocalizedChat::Presentation LocalizedChat::presentRetainedWithContext(uint64_t sequence,const swgproto::StringIdChatParameter& p,const ObjectContext& context){
    if(auto text=formatRetainedWithContext(sequence,p,context))return {*text,true};
    return {u"A localized message is unavailable.",false};
}
std::optional<std::u16string> LocalizedChat::lookup(const swgproto::StringId& id){
    if(!swgproto::isValidStringIdReference(id))return {};
    auto found=tables_.find(id.file);
    if(found==tables_.end()){
        if(passLoads_>=4)return {};
        Table table;++loads_;++passLoads_;
        try {auto raw=reader_("string/en/"+id.file+".stf");
            if(raw&&raw->size()<=maxTableBytes){table.values=decodeOriginalStringTable(*raw);
                if(table.values.size()>16384)table.values.clear();
                for(const auto& [key,text]:table.values)table.bytes+=key.size()+text.size()*2;
                if(table.bytes>maxTableBytes){table.values.clear();table.bytes=0;}
            }
        }catch(const std::exception&){table={};}
        while(!recent_.empty()&&(tables_.size()>=maxTables||bytes_+table.bytes>maxCacheBytes)){
            auto oldest=recent_.front();recent_.pop_front();bytes_-=tables_.at(oldest).bytes;tables_.erase(oldest);
        }
        bytes_+=table.bytes;found=tables_.emplace(id.file,std::move(table)).first;
    }else recent_.remove(id.file);
    recent_.push_back(id.file);
    auto key=found->second.values.find(id.stringId);
    if(key==found->second.values.end()||key->second.size()>maxOutput)return {};
    return key->second;
}
std::optional<std::u16string> LocalizedChat::participant(const swgproto::StringIdChatParticipant& p,const ObjectContext& context){
    if(p.objectId)if(auto object=context(p.objectId))return object->name;
    const bool hasId=!p.id.file.empty()||!p.id.stringId.empty();
    if(!p.text.empty()){
        if(p.text.size()>maxOutput||p.text.find(u'\0')!=p.text.npos)return {}; // Original segment decoding is a separate, unimplemented form.
        if(p.text.starts_with(u"@")){
            auto id=swgproto::parseStringIdReference(p.text);if(!id)return {};return lookup(*id);
        }
        return p.text;
    }
    if(hasId)return lookup(p.id);
    return {}; // Original preserves code3 when no participant/override exists; native defers.
}
std::optional<std::u16string> LocalizedChat::format(const swgproto::StringIdChatParameter& p,const ObjectName& names){
    return formatWithContext(p,[&](uint64_t id)->std::optional<ObjectInfo>{auto name=names(id);if(!name)return {};return ObjectInfo{*name};});
}
std::optional<std::u16string> LocalizedChat::formatWithContext(const swgproto::StringIdChatParameter& p,const ObjectContext& context){
    if(p.unknownByte)return {};
    auto input=lookup(p.id);if(!input||input->find(u'~')!=std::u16string::npos)return {};
    std::u16string output;output.reserve(input->size());
    for(size_t at=0;at<input->size();){
        if((*input)[at]!=u'%'||input->size()-at<3){if(output.size()==maxOutput)return {};output+=(*input)[at++];continue;}
        auto code=input->substr(at+1,2);std::optional<std::u16string> value;
        if(code==u"DI")value=unicode(std::to_string(std::bit_cast<int32_t>(p.di)));
        else if(code==u"DF")value=finiteFloat(p.df);
        else {
            const auto* participant=code[1]==u'U'?&p.tu:code[1]==u'T'?&p.tt:code[1]==u'O'?&p.to:nullptr;
            if(!participant)return {};
            const auto token=code[0];
            if(token==u'T'||token==u'N'){
                value=this->participant(*participant,context);
                if(value&&token==u'N'){
                    const auto first=value->find_first_not_of(u" \n\r\t");
                    if(first==value->npos)value=std::u16string{};
                    else {auto end=value->find_first_of(u" \n\r\t",first);value=value->substr(first,end==value->npos?value->size()-first:end-first);}
                }
            }else if(token==u'S'||token==u'O'||token==u'P'||token==u'F'||token==u'H'||token==u'M'){
                auto object=participant->objectId?context(participant->objectId):std::nullopt;
                if(!object){auto fallback=this->participant(*participant,context);if(!fallback||fallback->empty())return {};}
                if(token==u'F'||token==u'H'||token==u'M')value=std::u16string{};
                else {
                    int index=0;
                    if(object&&object->creature){
                        auto gender=object->gender;
                        if(!gender&&object->templatePath)gender=gender_.effective(*object->templatePath,object->alternateAppearance);
                        if(!gender)return {};index=*gender==0?1:*gender==1?2:0;
                    }
                    auto pronouns=lookup({"grammar",token==u'S'?"pronoun_subjective":token==u'O'?"pronoun_objective":"pronoun_possessive"});
                    if(!pronouns)return {};size_t start=0;
                    for(int line=0;line<index;++line){auto next=pronouns->find(u'\n',start);if(next==pronouns->npos)return {};start=next+1;}
                    auto end=pronouns->find(u'\n',start);value=pronouns->substr(start,end==pronouns->npos?pronouns->size()-start:end-start);
                    if(!value->empty()&&value->back()==u'\r')value->pop_back();
                }
            }else return {}; // Perspective/species/title and absent modern tokens remain deferred.
        }
        if(!value||value->size()>maxOutput-output.size())return {};
        output+=*value;at+=3; // Original fixed-three-character scan, never rescan substituted data.
    }
    return output;
}
}
