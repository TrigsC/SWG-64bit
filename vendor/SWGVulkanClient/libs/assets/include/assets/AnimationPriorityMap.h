#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace assets {

struct AnimationPriorityMapData {
    // Literal authored group names and signed priorities. Unknown names have
    // no inferred fallback; callers must explicitly handle a missing entry.
    std::unordered_map<std::string, int> groupPriorities;
};

class AnimationPriorityMap {
public:
    // FORM APRM / FORM 0000 / direct INFO: repeated NUL-terminated name,
    // little-endian signed int16 pairs until the chunk ends; there is no count.
    static AnimationPriorityMapData parse(const std::vector<uint8_t>& bytes);
};

} // namespace assets
