#pragma once
#include "soe/PacketBuffer.h"
#include <string>
namespace swgproto {
constexpr uint32_t kChatInstantMessageToCharacterHash=0x84bb21f7;
constexpr uint32_t kChatInstantMessageToClientHash=0x3c565ced;
constexpr uint32_t kChatOnSendInstantMessageHash=0x88dbb381;
struct ChatAvatarId {
 std::string game,galaxy,name;
 bool operator==(const ChatAvatarId&)const=default;
};
struct ChatInstantMessage {
 ChatAvatarId sender;std::u16string text,outOfBand;
 static ChatInstantMessage parse(soe::PacketBuffer&);
};
struct ChatInstantMessageResult {
 uint32_t result=0,sequence=0;
 static ChatInstantMessageResult parse(soe::PacketBuffer&);
};
// Original application operand count5: command + avatar + text + OOB + sequence.
std::vector<uint8_t> buildChatInstantMessage(const ChatAvatarId&,const std::u16string&,uint32_t sequence);
}
