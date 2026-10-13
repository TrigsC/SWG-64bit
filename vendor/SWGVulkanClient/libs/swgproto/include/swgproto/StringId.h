#pragma once

#include <string>
#include <cstdint>

namespace swgproto {

// Core3's StringId: a "file + string table key" pair used for
// server-catalog object names (e.g. "object/creature/player/human_male" +
// "human_male") - see BaseLineMessage::insertStringId, which inserts an
// int32(0) numeric-id field between the two ASCII strings. Core3 fills it
// with zero; retain it so unsupported numeric references cannot accidentally
// turn into an empty-key name fallback. Shared by both
// TangibleObject and IntangibleObject baselines (both use the same
// insertStringId helper for their objectName field), hence its own header
// rather than living inside either baseline's file.
struct StringId {
    std::string file;
    std::string stringId;
    uint32_t numericId=0;
    bool isInvalid() const {return file.empty()||(stringId.empty()&&numericId==0);}
};

} // namespace swgproto
