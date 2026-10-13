#pragma once
#include "swgproto/CommandQueueRemove.h"
#include "soe/MessageHash.h"
#include <cmath>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace clientcommon {
struct CommandTicket {
    uint32_t action=0,crc=0;
    uint64_t actor=0,target=0,submittedMs=0;
};
struct CommandEvent {
    enum class Kind {Admitted,TransportRejected,QueueCleared,NoReply};
    Kind kind=Kind::Admitted;
    CommandTicket ticket;
    uint32_t error=0,detail=0;
    float timer=0;
};
// Application outcomes remain distinct from transport ACKs and queue clears.
// Object identities are 64-bit; action IDs and CRCs retain their wire widths.
class CommandSubmission {
public:
    static constexpr size_t maxPending=64,maxEvents=256;
    std::optional<CommandTicket> reserve(uint64_t actor,uint64_t target,const std::string& name,uint64_t now) {
        std::lock_guard lock(mutex_);
        expireLocked(now);
        while(!recent_.empty()&&now>=recent_.front()&&now-recent_.front()>=1000)recent_.pop_front();
        if(!actor||pending_.size()>=maxPending||recent_.size()>=4||next_>0x3fffffe1)return {};
        CommandTicket ticket{next_,soe::MessageHash::compute(name),actor,target,now};next_+=32;
        pending_.emplace(ticket.action,ticket);recent_.push_back(now);return ticket;
    }
    void admitted(const CommandTicket& ticket,bool accepted) {
        std::lock_guard lock(mutex_);auto p=pending_.find(ticket.action);if(p==pending_.end())return;
        push({accepted?CommandEvent::Kind::Admitted:CommandEvent::Kind::TransportRejected,ticket});
        if(!accepted)pending_.erase(p);
    }
    bool receive(uint64_t actor,const swgproto::CommandQueueRemove& reply) {
        if(!std::isfinite(reply.timer)||reply.timer<0)return false;
        std::lock_guard lock(mutex_);auto p=pending_.find(reply.actionCount);
        if(p==pending_.end()||p->second.actor!=actor)return false;
        push({CommandEvent::Kind::QueueCleared,p->second,reply.tab1,reply.tab2,reply.timer});pending_.erase(p);return true;
    }
    std::vector<CommandEvent> drain(uint64_t now) {
        std::lock_guard lock(mutex_);expireLocked(now);
        std::vector<CommandEvent> out(events_.begin(),events_.end());events_.clear();return out;
    }
private:
    void push(CommandEvent event){if(events_.size()==maxEvents)events_.pop_front();events_.push_back(event);}
    void expireLocked(uint64_t now){for(auto p=pending_.begin();p!=pending_.end();){
        if(now>=p->second.submittedMs&&now-p->second.submittedMs>=10000){push({CommandEvent::Kind::NoReply,p->second});p=pending_.erase(p);}else ++p;}}
    std::mutex mutex_;uint32_t next_=1;
    std::map<uint32_t,CommandTicket> pending_;
    std::deque<uint64_t> recent_;std::deque<CommandEvent> events_;
};
}
