#pragma once
#include "swgproto/ChatRooms.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <tuple>
namespace clientcommon {
// This state belongs to one zone connection. Directory visibility is distinct
// from self membership. Packet parsers complete before any state is committed.
class ChatRoomDirectory {
public:
 struct Room {
  uint32_t id=0;std::optional<swgproto::ChatRoomData> data;
  bool joined=false,rosterKnown=false;
  std::vector<swgproto::ChatAvatarId> users,invited,moderators,banned;
  bool operator==(const Room&)const=default;
 };
 struct Snapshot {
  uint64_t revision=0;bool avatarConnectSeen=false,directoryReceived=false;
  std::map<uint32_t,Room> rooms;
  bool operator==(const Snapshot&)const=default;
 };
 explicit ChatRoomDirectory(swgproto::ChatAvatarId self):self_(std::move(self)){}
 Snapshot snapshot()const{std::lock_guard lock(mutex_);return state_;}
 // Notification does not reset membership: servers can deliver it more than once.
 bool connected(){std::lock_guard lock(mutex_);if(state_.avatarConnectSeen)return false;state_.avatarConnectSeen=true;++state_.revision;return true;}
 bool list(const swgproto::ChatRoomList& message){
  std::lock_guard lock(mutex_);auto next=state_;std::set<uint32_t> ids;
  for(const auto& data:message.rooms){
   if(!ids.insert(data.id).second||!upsert(next,data))return false;
  }
  next.directoryReceived=true;return commit(std::move(next));
 }
 bool created(const swgproto::ChatRoomCreated& message){
  if(message.result)return false;std::lock_guard lock(mutex_);auto next=state_;
  if(!upsert(next,message.room))return false;auto& r=next.rooms.at(message.room.id);
  r.invited=unique(message.room.invitees);r.moderators=unique(message.room.moderators);
  // The nested second vector is invitees in the original client, although
  // Core3 fills it with players in create notifications. It is not a roster.
  return commit(std::move(next));
 }
 bool query(const swgproto::ChatRoomQuery& message){
  if(!validMembers(message.users)||!validMembers(message.invited)||!validMembers(message.moderators)||!validMembers(message.banned))return false;
  std::lock_guard lock(mutex_);auto next=state_;if(!upsert(next,message.room))return false;
  auto& r=next.rooms.at(message.room.id);r.users=unique(message.users);r.invited=unique(message.invited);
  r.moderators=unique(message.moderators);r.banned=unique(message.banned);r.rosterKnown=true;
  return commit(std::move(next));
 }
 bool entered(const swgproto::ChatRoomMembership& message){return membership(message,true);}
 bool left(const swgproto::ChatRoomMembership& message){return membership(message,false);}
 bool destroyed(const swgproto::ChatRoomMembership& message){
  if(message.result||message.sequence||!message.id)return false;std::lock_guard lock(mutex_);
  if(!state_.rooms.erase(message.id))return false;++state_.revision;return true;
 }
 void reset(){std::lock_guard lock(mutex_);auto revision=state_.revision+1;state_={};state_.revision=revision;}
 std::optional<Room> byPath(const std::string& path)const{
  auto key=fold(path);std::lock_guard lock(mutex_);for(const auto& [_,r]:state_.rooms)if(r.data&&fold(r.data->path)==key)return r;return {};
 }
 std::optional<uint32_t> joinedRoomByLeaf(const std::string& leaf)const{
  auto key=fold(leaf);std::optional<uint32_t> out;std::lock_guard lock(mutex_);
  for(const auto& [id,r]:state_.rooms)if(r.joined&&r.data){
   const auto& path=r.data->path;auto dot=path.rfind('.');auto name=path.substr(dot==std::string::npos?0:dot+1);
   if(fold(name)==key){if(out)return {};out=id;}
  }
  return out;
 }
 static bool sameAvatar(const swgproto::ChatAvatarId& a,const swgproto::ChatAvatarId& b){return fold(a.game)==fold(b.game)&&fold(a.galaxy)==fold(b.galaxy)&&fold(a.name)==fold(b.name);}
private:
 static std::string fold(std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c+=char('a'-'A');return value;}
 static bool validString(const std::string& s,bool nonempty=false){return (!nonempty||!s.empty())&&s.size()<=1024&&std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=32&&c<=126;});}
 static bool validAvatar(const swgproto::ChatAvatarId& a){return validString(a.game)&&validString(a.galaxy)&&validString(a.name);}
 static bool validMembers(const std::vector<swgproto::ChatAvatarId>& v){return v.size()<=swgproto::kMaxRoomAvatars&&std::all_of(v.begin(),v.end(),validAvatar);}
 static std::vector<swgproto::ChatAvatarId> unique(const std::vector<swgproto::ChatAvatarId>& values){
  std::vector<swgproto::ChatAvatarId> out;std::set<std::tuple<std::string,std::string,std::string>> keys;
  for(const auto& a:values)if(keys.emplace(fold(a.game),fold(a.galaxy),fold(a.name)).second)out.push_back(a);return out;
 }
 static bool upsert(Snapshot& next,const swgproto::ChatRoomData& data){
  if(!data.id||!validString(data.path,true)||data.privateFlag>1||data.moderated>1||data.title.size()>8192||
     !validAvatar(data.owner)||!validAvatar(data.creator)||!validMembers(data.invitees)||!validMembers(data.moderators))return false;
  auto path=fold(data.path);
  for(const auto& [id,r]:next.rooms)if(id!=data.id&&r.data&&fold(r.data->path)==path)return false;
  auto p=next.rooms.find(data.id);
  if(p!=next.rooms.end()&&p->second.data&&fold(p->second.data->path)!=path)return false;
  if(p==next.rooms.end()&&next.rooms.size()>=swgproto::kMaxChatRooms)return false;
  auto& r=next.rooms[data.id];r.id=data.id;r.data=data;return true;
 }
 bool membership(const swgproto::ChatRoomMembership& message,bool enter){
  if(message.result||!message.id||!validAvatar(message.avatar)||message.avatar.name.empty())return false;
  std::lock_guard lock(mutex_);auto next=state_;auto p=next.rooms.find(message.id);
  if(p==next.rooms.end()){
   // Unknown leave cannot create a room. Unknown successful entry is retained
   // until a singleton/full directory packet supplies its identity.
   if(!enter||next.rooms.size()>=swgproto::kMaxChatRooms)return false;
   p=next.rooms.emplace(message.id,Room{}).first;p->second.id=message.id;
  }
  auto& r=p->second;auto found=std::find_if(r.users.begin(),r.users.end(),[&](const auto& a){return sameAvatar(a,message.avatar);});
  if(enter&&found==r.users.end()){if(r.users.size()>=swgproto::kMaxRoomAvatars)return false;r.users.push_back(message.avatar);}
  if(!enter&&found!=r.users.end())r.users.erase(found);
  if(sameAvatar(self_,message.avatar))r.joined=enter;
  return commit(std::move(next));
 }
 bool commit(Snapshot next){
  size_t avatars=0;
  for(const auto& [_,r]:next.rooms){
   for(const auto* v:{&r.users,&r.invited,&r.moderators,&r.banned}){if(!validMembers(*v))return false;avatars+=v->size();}
   if(r.data)avatars+=r.data->invitees.size()+r.data->moderators.size();
  }
  if(avatars>swgproto::kMaxRoomPacketAvatars||next==state_)return false;
  next.revision=state_.revision+1;state_=std::move(next);return true;
 }
 mutable std::mutex mutex_;swgproto::ChatAvatarId self_;Snapshot state_;
};
}
