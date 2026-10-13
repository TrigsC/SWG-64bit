#pragma once
#include "soe/PacketBuffer.h"
#include "StringId.h"
#include <cstdint>
#include <optional>
#include <string>
namespace swgproto {
struct StringIdChatParticipant { uint64_t objectId=0; StringId id; std::u16string text; };
struct StringIdChatParameter {
    StringId id;
    StringIdChatParticipant tu,tt,to;
    uint32_t di=0;
    float df=0;
    uint8_t unknownByte=0;
};
constexpr size_t kMaxChatParameterBytes=65536;
constexpr size_t kMaxChatUnicodeUnits=8192;
// Bound lengths before allocation. Reference validation is deliberately separate
// from wire decoding; unknown reference names never become filesystem paths.
std::u16string readBoundedChatUnicode(soe::PacketBuffer&);
bool isValidStringIdReference(const StringId&);
std::optional<StringId> parseStringIdReference(const std::u16string&);
// Reads precisely byteCount bytes. An unknown type/marker is safely consumed
// and returns nullopt. Recognized malformed/truncated StringId containers throw.
std::optional<StringIdChatParameter> parseStringIdChatParameter(soe::PacketBuffer&,size_t byteCount);
} // namespace swgproto
