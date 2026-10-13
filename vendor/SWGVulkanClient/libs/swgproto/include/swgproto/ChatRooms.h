#pragma once
#include "swgproto/ChatInstantMessage.h"
#include <vector>
namespace swgproto {
constexpr uint32_t kChatOnConnectAvatarHash=0xd72fe9be;
constexpr uint32_t kChatRoomListHash=0x70deb197;
constexpr uint32_t kChatQueryRoomResultsHash=0xc4de864e;
constexpr uint32_t kChatOnEnteredRoomHash=0xe69bdc0a;
constexpr uint32_t kChatOnLeaveRoomHash=0x60b5098b;
constexpr uint32_t kChatOnCreateRoomHash=0x35d7cc9f;
constexpr uint32_t kChatOnDestroyRoomHash=0xe8ec5877;
constexpr uint32_t kChatRequestRoomListHash=0x4c3d2cfa;
constexpr size_t kMaxChatRooms=4096,kMaxRoomAvatars=4096,kMaxRoomPacketAvatars=16384;
struct ChatRoomData {
 uint32_t id=0,privateFlag=0;uint8_t moderated=0;
 std::string path;ChatAvatarId owner,creator;std::u16string title;
 std::vector<ChatAvatarId> moderators,invitees;
 bool operator==(const ChatRoomData&)const=default;
};
struct ChatRoomList {std::vector<ChatRoomData> rooms;static ChatRoomList parse(soe::PacketBuffer&);};
struct ChatRoomQuery {
 std::vector<ChatAvatarId> users,invited,moderators,banned;
 uint32_t sequence=0;ChatRoomData room;
 static ChatRoomQuery parse(soe::PacketBuffer&);
};
struct ChatRoomMembership {
 ChatAvatarId avatar;uint32_t result=0,id=0,sequence=0;
 static ChatRoomMembership parse(soe::PacketBuffer&);
};
struct ChatRoomCreated {
 uint32_t result=0;ChatRoomData room;uint32_t sequence=0;
 static ChatRoomCreated parse(soe::PacketBuffer&);
};
// Connect has no operands; malformed payloads cannot trigger bootstrap requests.
void parseChatConnected(soe::PacketBuffer&);
std::vector<uint8_t> buildChatRequestRoomList();
}
