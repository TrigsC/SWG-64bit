#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <optional>
#include <array>
#include "swgproto/StringIdChatParameter.h"
#include "ChatChannels.h"

namespace clientcommon {
struct ChatLine {
    enum class Kind {System,Spatial,Instant};
    enum class Delivery {Channel,AllTabs,CurrentTab};
    Kind kind=Kind::System;
    uint64_t sequence=0,sender=0,target=0;
    uint8_t displayType=0,language=0,flags=0;
    uint16_t chatType=0,mood=0;
    std::u16string text;
    std::optional<swgproto::StringIdChatParameter> localized;
    Delivery delivery=Delivery::Channel;
    std::optional<ChatChannelId> channel;
};
struct ChatSnapshot {uint64_t revision=0,evicted=0,unsupported=0;std::vector<ChatLine> lines;};
class ChatLog {
public:
    static constexpr size_t maxLines=256,maxCharacters=8192;
    void clear(){std::lock_guard lock(mutex_);lines_.clear();evicted_=unsupported_=0;for(auto& tab:tabs_)tab={};routing_.select(0);++revision_;++viewRevision_;}
    void append(ChatLine line) {
        std::lock_guard lock(mutex_);
        if(line.text.empty()&&!line.localized)return;
        const auto bounded=[](const swgproto::StringIdChatParameter& p){
            const auto id=[](const swgproto::StringId& s){return s.file.size()<=1024&&s.stringId.size()<=1024;};
            const auto actor=[&](const swgproto::StringIdChatParticipant& s){return id(s.id)&&s.text.size()<=maxCharacters;};
            return id(p.id)&&actor(p.tu)&&actor(p.tt)&&actor(p.to);
        };
        if(line.text.size()>maxCharacters||(line.localized&&!bounded(*line.localized))){++unsupported_;++revision_;return;}
        line.sequence=++revision_;
        if(lines_.size()==maxLines){lines_.pop_front();++evicted_;}
        auto type=line.kind==ChatLine::Kind::Spatial?ChatChannelType::Spatial:line.kind==ChatLine::Kind::Instant?ChatChannelType::Instant:systemChatChannel(line.displayType);
        auto destination=line.delivery==ChatLine::Delivery::CurrentTab?routing_.currentOnly():routing_.route(line.delivery==ChatLine::Delivery::AllTabs?ChatChannelId{}:line.channel.value_or(*ChatChannelId::builtin(type)));
        for(auto index:destination.indices){auto& tab=tabs_[index];if(tab.lines.size()==maxLines){tab.lines.pop_front();++tab.evicted;}tab.lines.push_back(line);}
        if(!destination.indices.empty())++viewRevision_;
        lines_.push_back(std::move(line));
    }
    void unsupported(){std::lock_guard lock(mutex_);++unsupported_;++revision_;}
    bool snapshotIfChanged(uint64_t revision,ChatSnapshot& out) const {
        std::lock_guard lock(mutex_);if(revision==revision_)return false;
        out={revision_,evicted_,unsupported_,{lines_.begin(),lines_.end()}};return true;
    }
    bool selectTab(size_t index){std::lock_guard lock(mutex_);if(index==routing_.active()||!routing_.select(index))return false;++viewRevision_;return true;}
    size_t activeTab()const{std::lock_guard lock(mutex_);return routing_.active();}
    std::array<size_t,4> tabLineCounts()const{std::lock_guard lock(mutex_);std::array<size_t,4> result{};for(size_t i=0;i<4;++i)result[i]=tabs_[i].lines.size();return result;}
    std::vector<uint64_t> retainedSequences()const{
        std::lock_guard lock(mutex_);std::vector<uint64_t> out;out.reserve(maxLines*5);
        for(const auto& line:lines_)out.push_back(line.sequence);for(const auto& tab:tabs_)for(const auto& line:tab.lines)out.push_back(line.sequence);
        std::sort(out.begin(),out.end());out.erase(std::unique(out.begin(),out.end()),out.end());return out;
    }
    bool snapshotActiveIfChanged(uint64_t revision,ChatSnapshot& out)const{
        std::lock_guard lock(mutex_);if(revision==viewRevision_)return false;const auto& tab=tabs_[routing_.active()];out={viewRevision_,tab.evicted,unsupported_,{tab.lines.begin(),tab.lines.end()}};return true;
    }
private:
    struct TabHistory {std::deque<ChatLine> lines;uint64_t evicted=0;};
    ChatTabRouting routing_=ChatTabRouting::originalDefaults();
    std::array<TabHistory,4> tabs_{};
    uint64_t viewRevision_=0;
    mutable std::mutex mutex_;
    std::deque<ChatLine> lines_;
    uint64_t revision_=0,evicted_=0,unsupported_=0;
};
}
