#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include "soe/PacketBuffer.h"
#include "StringIdChatParameter.h"
namespace swgproto {
constexpr uint32_t kChatSystemMessageHash = 0x6D2A6413;
// Both source constructor shapes share this hash. paramsSize counts two-byte
// words, never bytes. A supported localized payload is retained structurally.
struct ChatSystemMessage {
    enum DisplayType : uint8_t {DisplayChatAndScreen=0x00,DisplayChatOnly=0x02,DisplayQuest=0x04};
    uint8_t displayType=0;
    std::u16string message;
    uint32_t paramsSize=0;
    std::optional<StringIdChatParameter> localized;
    static ChatSystemMessage parse(soe::PacketBuffer& buf);
};
}
