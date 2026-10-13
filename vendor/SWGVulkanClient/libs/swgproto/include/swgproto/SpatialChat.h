#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include "StringIdChatParameter.h"

#include "soe/PacketBuffer.h"

namespace swgproto {

// header2 for this sub-message - confirmed via Core3's SpatialChat.h:
// ObjectControllerMessage(targetID, 0x0B, 0xF4).
constexpr uint32_t kSpatialChatControllerType = 0xF4;

// Ordinary Unicode and exact @file:key references use the fixed zero tail.
// Parameterized speech instead has an empty Unicode field, parameter word
// count, bounded ChatParameter, and one final zero32. Empty ordinary speech
// remains distinguishable by its sixteen-byte tail.
struct SpatialChat {
    uint64_t senderId = 0;
    uint64_t chatTargetId = 0;
    std::u16string message;
    std::optional<StringIdChatParameter> localized;
    uint32_t paramsSize = 0;
    bool parameterized = false;
    uint16_t volume = 0;
    uint16_t spatialChatType = 0;
    uint16_t moodType = 0;
    uint8_t chatFlags = 0;
    uint8_t languageId = 0;
    // Trailing insertLong(0)/insertInt(0)/insertInt(0) from source - always
    // zero in the real capture, kept as real (if inert) fields for fidelity
    // rather than assumed and skipped.
    uint64_t unknownTrailingLong = 0;
    int32_t unknownTrailingInt1 = 0;
    int32_t unknownTrailingInt2 = 0;

    // Parses the fields following the shared ObjControllerMessage envelope
    // (header1/header2/objectId/unused already consumed).
    static SpatialChat parse(soe::PacketBuffer& buf);
};

} // namespace swgproto
