#include "swgproto/ChatRooms.h"
#include "swgproto/StringIdChatParameter.h"
#include <stdexcept>
namespace swgproto {
namespace {
std::string ascii(soe::PacketBuffer& b){
 auto n=b.readUint16();if(n>1024||n>b.remaining())throw std::runtime_error("Invalid room identity extent");
 auto bytes=b.readBytes(n);for(auto c:bytes)if(c<32||c>126)throw std::runtime_error("Invalid room identity character");return {bytes.begin(),bytes.end()};
}
ChatAvatarId readAvatar(soe::PacketBuffer& b){return {ascii(b),ascii(b),ascii(b)};}
std::vector<ChatAvatarId> avatars(soe::PacketBuffer& b,size_t& budget){
 auto n=b.readUint32();if(n>kMaxRoomAvatars||n>budget||n>b.remaining()/6)throw std::runtime_error("Invalid room avatar count");budget-=n;
 std::vector<ChatAvatarId> out;out.reserve(n);for(uint32_t i=0;i<n;++i)out.push_back(readAvatar(b));return out;
}
ChatRoomData readRoom(soe::PacketBuffer& b,size_t& budget){
 ChatRoomData r;r.id=b.readUint32();r.privateFlag=b.readUint32();r.moderated=b.readByte();r.path=ascii(b);
 r.owner=readAvatar(b);r.creator=readAvatar(b);r.title=readBoundedChatUnicode(b);r.moderators=avatars(b,budget);r.invitees=avatars(b,budget);return r;
}
void end(soe::PacketBuffer& b){if(b.remaining())throw std::runtime_error("Trailing room message bytes");}
}
ChatRoomList ChatRoomList::parse(soe::PacketBuffer& b){
 ChatRoomList out;size_t budget=kMaxRoomPacketAvatars;auto n=b.readUint32();
 if(n>kMaxChatRooms||n>b.remaining()/35)throw std::runtime_error("Invalid room count");
 out.rooms.reserve(n);for(uint32_t i=0;i<n;++i)out.rooms.push_back(readRoom(b,budget));end(b);return out;
}
ChatRoomQuery ChatRoomQuery::parse(soe::PacketBuffer& b){
 ChatRoomQuery out;size_t budget=kMaxRoomPacketAvatars;
 out.users=avatars(b,budget);out.invited=avatars(b,budget);out.moderators=avatars(b,budget);out.banned=avatars(b,budget);
 out.sequence=b.readUint32();out.room=readRoom(b,budget);end(b);return out;
}
ChatRoomMembership ChatRoomMembership::parse(soe::PacketBuffer& b){
 ChatRoomMembership out;out.avatar=readAvatar(b);out.result=b.readUint32();out.id=b.readUint32();out.sequence=b.readUint32();end(b);return out;
}
ChatRoomCreated ChatRoomCreated::parse(soe::PacketBuffer& b){
 ChatRoomCreated out;size_t budget=kMaxRoomPacketAvatars;out.result=b.readUint32();out.room=readRoom(b,budget);out.sequence=b.readUint32();end(b);return out;
}
void parseChatConnected(soe::PacketBuffer& b){end(b);}
std::vector<uint8_t> buildChatRequestRoomList(){soe::PacketBuffer b;b.writeUint16(1);b.writeUint32(kChatRequestRoomListHash);return {b.data(),b.data()+b.size()};}
}
