#pragma once
#include "OriginalHudAssets.h"
#include "swgproto/StringIdChatParameter.h"
#include "TemplateGender.h"
#include <list>
#include <map>
#include <optional>
#include <span>

namespace dummyclient {
// Render-thread-only, bounded active-TRE lookup. No filesystem/network work on receive.
class LocalizedChat {
public:
    using ObjectName=std::function<std::optional<std::u16string>(uint64_t)>;
    struct ObjectInfo {std::optional<std::u16string> name;bool creature=false;std::optional<int32_t> gender;std::optional<std::string> templatePath;std::string alternateAppearance;};
    using ObjectContext=std::function<std::optional<ObjectInfo>(uint64_t)>;
    explicit LocalizedChat(HudAssetReader reader):reader_(std::move(reader)),gender_(reader_){}
    std::optional<std::u16string> format(const swgproto::StringIdChatParameter&,const ObjectName&);
    std::optional<std::u16string> formatRetained(uint64_t sequence,const swgproto::StringIdChatParameter&,const ObjectName&);
    std::optional<std::u16string> formatWithContext(const swgproto::StringIdChatParameter&,const ObjectContext&);
    std::optional<std::u16string> formatRetainedWithContext(uint64_t sequence,const swgproto::StringIdChatParameter&,const ObjectContext&);
    std::optional<std::u16string> resolveName(const swgproto::StringId& id){return lookup(id);}
    void retainSequences(std::span<const uint64_t>);
    size_t resolvedLines() const{return resolved_.size();}
    void beginPass(){passLoads_=0;gender_.beginPass();}
    size_t cachedTables() const{return tables_.size();}
    size_t cachedTextBytes() const{return bytes_;}
    size_t tableLoads() const{return loads_;}
    size_t metadataLoads()const{return gender_.loads();}
    size_t cachedTemplates()const{return gender_.cachedTemplates();}
private:
    struct Table {std::map<std::string,std::u16string> values;size_t bytes=0;};
    std::optional<std::u16string> lookup(const swgproto::StringId&);
    std::optional<std::u16string> participant(const swgproto::StringIdChatParticipant&,const ObjectContext&);
    HudAssetReader reader_;std::map<std::string,Table> tables_;std::list<std::string> recent_;
    size_t bytes_=0,loads_=0,passLoads_=0;
    std::map<uint64_t,std::u16string> resolved_;
    TemplateGender gender_;
};
}
