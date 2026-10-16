#pragma once

#include "clientcommon/CommandSubmission.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <optional>

namespace clientcommon {

// The first four slots deliberately use the same command catalog and queue as
// typed commands. Only a server queue reply can start a cooldown.
class ActionToolbar {
public:
    static constexpr size_t count=4;
    enum class State {Unavailable,NeedsTarget,Ready,Pending,Cooldown,Rejected,Unknown};
    struct Slot {
        std::string name;
        std::u16string label;
        bool available=false,targetRequired=false;
        uint32_t pendingAction=0,error=0;
        uint64_t readyAt=0;
        bool rejected=false,unknown=false;
    };
    void configure(size_t index,std::string name,std::u16string label,bool available,bool targetRequired){
        if(index<count)slots_[index]={std::move(name),std::move(label),available,targetRequired};
    }
    const Slot& slot(size_t index) const{return slots_.at(index);}
    State state(size_t index,bool hasTarget,uint64_t now) const{
        const auto& s=slot(index);
        if(!s.available)return State::Unavailable;
        if(s.pendingAction)return State::Pending;
        if(s.readyAt>now)return State::Cooldown;
        if(s.targetRequired&&!hasTarget)return State::NeedsTarget;
        if(s.rejected)return State::Rejected;
        if(s.unknown)return State::Unknown;
        return State::Ready;
    }
    std::optional<std::string> request(size_t index,bool hasTarget,bool textFocused,uint64_t now) const{
        if(index>=count||textFocused)return {};
        const auto value=state(index,hasTarget,now);
        if(value==State::Ready||value==State::Rejected||value==State::Unknown)return slots_[index].name;
        return {};
    }
    void onEvent(const CommandEvent& event,uint64_t now){
        for(auto& s:slots_){
            if(!s.available||soe::MessageHash::compute(s.name)!=event.ticket.crc)continue;
            if(s.pendingAction&&s.pendingAction!=event.ticket.action)continue;
            switch(event.kind){
            case CommandEvent::Kind::Admitted:
                s.pendingAction=event.ticket.action;s.rejected=s.unknown=false;s.error=0;break;
            case CommandEvent::Kind::TransportRejected:
                s.pendingAction=0;s.rejected=true;s.unknown=false;s.error=0;break;
            case CommandEvent::Kind::NoReply:
                s.pendingAction=0;s.unknown=true;s.rejected=false;s.error=0;break;
            case CommandEvent::Kind::QueueCleared:
                s.pendingAction=0;s.error=event.error;s.rejected=event.error!=0;s.unknown=false;
                if(!event.error&&std::isfinite(event.timer)&&event.timer>0)
                    s.readyAt=now+uint64_t(std::min(event.timer,3600.f)*1000.f);
                else s.readyAt=0;
                break;
            }
        }
    }
private:
    std::array<Slot,count> slots_{};
};
}
