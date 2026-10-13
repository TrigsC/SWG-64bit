#include "assets/AnimationPriorityMap.h"

#include <stdexcept>
#include <utility>

#include "assets/IffReader.h"

namespace assets {

AnimationPriorityMapData AnimationPriorityMap::parse(const std::vector<uint8_t>& bytes) {
    const auto top = IffReader::parse(bytes);
    if (top.size() != 1 || top.front().id != kFormTag || top.front().formType != 0x4150524d) {
        throw std::runtime_error("AnimationPriorityMap: expected FORM APRM");
    }
    const auto& root = top.front();
    if (root.children.size() != 1 || root.children.front().id != kFormTag ||
        root.children.front().formType != 0x30303030) {
        throw std::runtime_error("AnimationPriorityMap: unsupported version");
    }
    const auto& version = root.children.front();
    if (version.children.size() != 1 || version.children.front().id != 0x494e464f) {
        throw std::runtime_error("AnimationPriorityMap: expected direct INFO");
    }
    auto buffer = version.children.front().data;
    buffer.resetReadCursor();
    AnimationPriorityMapData result;
    while (buffer.remaining() != 0) {
        auto name = readNulTerminatedString(buffer);
        const int priority = static_cast<int16_t>(buffer.readUint16());
        if (name.empty() || !result.groupPriorities.emplace(std::move(name), priority).second) {
            throw std::runtime_error("AnimationPriorityMap: empty or duplicate group name");
        }
    }
    return result;
}

} // namespace assets
