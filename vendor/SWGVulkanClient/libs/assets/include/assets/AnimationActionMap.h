#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace assets {

struct AnimationActionMapping {
    std::string animationStateName; // Literal LAT state name, not a clip path.
    std::array<uint8_t, 2> opaqueMetadata{}; // Preserve bytes; no inferred channel semantics.
};

struct AnimationActionMapData {
    std::string rootStateName;
    std::string rootAnimationStateName;
    std::unordered_map<std::string, AnimationActionMapping> actions;
    size_t skippedMultiActionCount = 0; // Direct MVAC groups are not flattened into aliases.
};

class AnimationActionMap {
public:
    // ASHT/0002 root STAT's own ACTS only. Each direct ACTN contains two
    // NUL strings (action name, LAT state) followed by exactly two metadata
    // bytes. Descendant state overrides and grouped MVAC routing are outside
    // this parser's scope. Malformed, duplicate or truncated mappings throw.
    static AnimationActionMapData parse(const std::vector<uint8_t>& bytes);
};

} // namespace assets
