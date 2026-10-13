#pragma once
#include "swgproto/ChatInstantMessage.h"
#include <deque>
#include <map>
#include <mutex>
#include <optional>
namespace clientcommon {
struct TellTicket {uint32_t sequence=0;uint64_t submittedMs=0;swgproto::ChatAvatarId recipient;std::u16string text;bool reply=false;};
struct TellEvent {
 enum class Kind {Admitted,TransportRejected,Result,NoReply};Kind kind=Kind::Admitted;TellTicket ticket;uint32_t result=0;
};
// App acknowledgement is distinct from SOE ACK; all state is per connection.
// Early app replies are held until transport admission has been recorded.
class TellSubmission {
public:
 struct Context {swgproto::ChatAvatarId self;std::optional<swgproto::ChatAvatarId> incoming,outgoing;};
 explicit TellSubmission(swgproto::ChatAvatarId self):self_(std::move(self)){}
 Context context()const{std::lock_guard lock(mutex_);return {self_,incoming_,outgoing_};}
 std::optional<TellTicket> reserve(const swgproto::ChatAvatarId& to,const std::u16string& text,bool reply,uint64_t now){
  auto tickets=reserveBatch({to},text,reply,now);if(!tickets)return {};return tickets->front();
 }
 std::optional<std::vector<TellTicket>> reserveBatch(const std::vector<swgproto::ChatAvatarId>& targets,const std::u16string& text,bool reply,uint64_t now){
  std::lock_guard lock(mutex_);expire(now);while(!recent_.empty()&&now>=recent_.front()&&now-recent_.front()>=1000)recent_.pop_front();
  if(targets.empty()||targets.size()>4||pending_.size()+targets.size()>64||recent_.size()+targets.size()>4||text.empty()||text.size()>512)return {};
  for(const auto& to:targets)if(to.name.empty()||to.name.size()>1024||to.game.size()>1024||to.galaxy.size()>1024)return {};
  if(targets.size()>UINT32_MAX-next_)return {};std::vector<TellTicket> out;
  for(const auto& to:targets){TellTicket t{++next_,now,to,text,reply};pending_.emplace(t.sequence,Pending{t});recent_.push_back(now);out.push_back(std::move(t));}return out;
 }
 void admitted(const TellTicket& t,bool accepted){
  std::lock_guard lock(mutex_);auto p=pending_.find(t.sequence);if(p==pending_.end()||p->second.accepted)return;
  push({accepted?TellEvent::Kind::Admitted:TellEvent::Kind::TransportRejected,t});
  if(!accepted)pending_.erase(p);else {p->second.accepted=true;if(!t.reply)outgoing_=t.recipient;}
 }
 bool receive(const swgproto::ChatInstantMessageResult& r){
  std::lock_guard lock(mutex_);auto p=pending_.find(r.sequence);if(p==pending_.end()||p->second.result)return false;p->second.result=r.result;return true;
 }
 void received(const swgproto::ChatAvatarId& from){std::lock_guard lock(mutex_);incoming_=from;}
 std::vector<TellEvent> drain(uint64_t now){
  std::lock_guard lock(mutex_);
  for(auto p=pending_.begin();p!=pending_.end();)if(p->second.accepted&&p->second.result){push({TellEvent::Kind::Result,p->second.ticket,*p->second.result});p=pending_.erase(p);}else ++p;
  expire(now);std::vector<TellEvent> out(events_.begin(),events_.end());events_.clear();return out;
 }
 size_t pending()const{std::lock_guard lock(mutex_);return pending_.size();}
private:
 struct Pending {TellTicket ticket;bool accepted=false;std::optional<uint32_t> result;};
 void push(TellEvent e){if(events_.size()==256)events_.pop_front();events_.push_back(std::move(e));}
 void expire(uint64_t now){for(auto p=pending_.begin();p!=pending_.end();)if(now>=p->second.ticket.submittedMs&&now-p->second.ticket.submittedMs>=60000){
  if(p->second.accepted&&p->second.result)push({TellEvent::Kind::Result,p->second.ticket,*p->second.result});else push({TellEvent::Kind::NoReply,p->second.ticket});p=pending_.erase(p);
 }else ++p;}
 mutable std::mutex mutex_;swgproto::ChatAvatarId self_;std::optional<swgproto::ChatAvatarId> incoming_,outgoing_;uint32_t next_=0;
 std::map<uint32_t,Pending> pending_;std::deque<uint64_t> recent_;std::deque<TellEvent> events_;
};
}
