#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace clientcommon {
// Installed Pre-CU IDs, not the later client-tools enum (City/GCW differ).
enum class ChatChannelType : uint32_t {
    None=0, Room=1, Spatial=2, Planet=3, Combat=4, System=5,
    Instant=6, Group=7, MatchMaking=8, Guild=9, Named=10, Quest=11
};
class ChatChannelId {
public:
    static constexpr size_t maxNameBytes=1024;
    ChatChannelId()=default;
    static std::optional<ChatChannelId> make(ChatChannelType type,std::string_view name) {
        if(static_cast<uint32_t>(type)>11||name.empty()||name.size()>maxNameBytes)return {};
        ChatChannelId id;id.type_=type;id.name_=name;id.lower_=name;
        // Native scope: printable ASCII channel paths; no locale/Unicode guess.
        for(char& c:id.lower_){const auto b=static_cast<unsigned char>(c);if(b<32||b>126)return {};if(c>='A'&&c<='Z')c+=32;}
        return id;
    }
    static std::optional<ChatChannelId> builtin(ChatChannelType type) {
        switch(type){
        case ChatChannelType::None:return make(type,"none");
        case ChatChannelType::Spatial:return make(type,"spatial");
        case ChatChannelType::Planet:return make(type,"planet");
        case ChatChannelType::Combat:return make(type,"combat");
        case ChatChannelType::System:return make(type,"system");
        case ChatChannelType::Instant:return make(type,"im");
        case ChatChannelType::Group:return make(type,"group");
        case ChatChannelType::MatchMaking:return make(type,"matchMaking");
        case ChatChannelType::Guild:return make(type,"guild");
        case ChatChannelType::Quest:return make(type,"quest");
        default:return {};
        }
    }
    ChatChannelType type()const{return type_;}
    const std::string& name()const{return name_;}
    bool operator==(const ChatChannelId& rhs)const{return type_==rhs.type_&&lower_==rhs.lower_;}
    bool operator<(const ChatChannelId& rhs)const{return type_<rhs.type_||(type_==rhs.type_&&lower_<rhs.lower_);}
private:
    ChatChannelType type_=ChatChannelType::None;
    std::string name_="none",lower_="none";
};

class ChatTabFilter {
public:
    static constexpr size_t maxChannels=64;
    bool has(const ChatChannelId& id)const{return std::binary_search(channels_.begin(),channels_.end(),id);}
    bool add(const ChatChannelId& id){
        if(id.type()==ChatChannelType::None)return false;
        auto at=std::lower_bound(channels_.begin(),channels_.end(),id);
        if(at!=channels_.end()&&*at==id)return true;
        if(channels_.size()==maxChannels)return false;
        const bool empty=channels_.empty();channels_.insert(at,id);if(empty)default_=id;return true;
    }
    bool remove(const ChatChannelId& id){
        auto at=std::lower_bound(channels_.begin(),channels_.end(),id);
        const bool found=at!=channels_.end()&&*at==id;
        if(found)channels_.erase(at);
        // Original compares the default even when erase found no membership.
        if(default_==id)default_=channels_.empty()?ChatChannelId{}:channels_.front();return found;
    }
    bool setDefault(const ChatChannelId& id){
        if(id.type()!=ChatChannelType::None&&!add(id))return false;default_=id;return true;
    }
    const ChatChannelId& defaultChannel()const{return default_;}
    const std::vector<ChatChannelId>& channels()const{return channels_;}
private:
    ChatChannelId default_;
    std::vector<ChatChannelId> channels_;
};

struct ChatTabDestinations {
    std::vector<size_t> indices;
    std::optional<size_t> created;
    bool capacityRejected=false;
};
class ChatTabRouting {
public:
    static constexpr size_t maxTabs=32;
    static ChatTabRouting originalDefaults(bool space=false){
        ChatTabRouting routing;
        // Exact original tables, including their repeated Planet/Quest entries.
        const std::vector<std::vector<ChatChannelType>> ground={
            {ChatChannelType::Spatial,ChatChannelType::Quest,ChatChannelType::Instant,ChatChannelType::System,ChatChannelType::Group,ChatChannelType::MatchMaking},
            {ChatChannelType::Combat,ChatChannelType::Instant,ChatChannelType::System,ChatChannelType::MatchMaking},
            {ChatChannelType::Planet,ChatChannelType::Planet},
            {ChatChannelType::Quest,ChatChannelType::Quest}};
        for(size_t i=0;i<ground.size();++i){if(space&&i!=0&&i!=3)continue;ChatTabFilter tab;
            for(auto type:ground[i])tab.add(*ChatChannelId::builtin(type));routing.addTab(std::move(tab));}
        return routing;
    }
    std::optional<size_t> addTab(ChatTabFilter filter){
        if(tabs_.size()==maxTabs)return {};tabs_.push_back(std::move(filter));return tabs_.size()-1;
    }
    bool select(size_t index){if(index>=tabs_.size())return false;active_=index;return true;}
    size_t active()const{return active_;}
    const std::vector<ChatTabFilter>& tabs()const{return tabs_;}
    ChatTabDestinations route(const ChatChannelId& id,bool create=false){
        ChatTabDestinations out;
        for(size_t i=0;i<tabs_.size();++i)if(id.type()==ChatChannelType::None||tabs_[i].has(id))out.indices.push_back(i);
        // Original create=true fallback; later source comments this out.
        if(out.indices.empty()&&create){ChatTabFilter tab;tab.setDefault(id);auto index=addTab(std::move(tab));
            if(index){out.created=index;out.indices.push_back(*index);}else out.capacityRejected=true;}
        return out;
    }
    ChatTabDestinations currentOnly()const{
        ChatTabDestinations out;if(active_<tabs_.size())out.indices.push_back(active_);return out;
    }
private:
    std::vector<ChatTabFilter> tabs_;
    size_t active_=0;
};
// This is the original UI manager flag, not a new wire decoder. Live support
// for quest packet flags remains gated by the existing receive implementation.
inline ChatChannelType systemChatChannel(uint8_t flags){return (flags&4)?ChatChannelType::Quest:ChatChannelType::System;}
}
