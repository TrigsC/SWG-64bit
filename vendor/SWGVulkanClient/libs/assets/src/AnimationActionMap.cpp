#include "assets/AnimationActionMap.h"

#include <stdexcept>
#include <utility>

#include "assets/IffReader.h"

namespace assets {
namespace {
constexpr uint32_t kInfo = 0x494e464f;
constexpr uint32_t kStat = 0x53544154;
constexpr uint32_t kActs = 0x41435453;
constexpr uint32_t kActn = 0x4143544e;
constexpr uint32_t kMvac = 0x4d564143;

uint32_t bigEndian32(const std::vector<uint8_t>& bytes, size_t position) {
    return (uint32_t(bytes[position]) << 24) | (uint32_t(bytes[position + 1]) << 16) |
           (uint32_t(bytes[position + 2]) << 8) | uint32_t(bytes[position + 3]);
}

// IffReader tolerates a final fragment shorter than a chunk header. Require
// exact structural consumption here, before constructing its recursive tree.
void validateChunks(const std::vector<uint8_t>& bytes, size_t start, size_t end, unsigned depth) {
    if (depth > 128) throw std::runtime_error("AnimationActionMap: excessive FORM nesting");
    while (start < end) {
        if (end - start < 8) throw std::runtime_error("AnimationActionMap: truncated chunk header");
        const auto tag = bigEndian32(bytes, start);
        const auto size = bigEndian32(bytes, start + 4);
        start += 8;
        if (size > end - start) throw std::runtime_error("AnimationActionMap: truncated chunk payload");
        if (tag == kFormTag) {
            if (size < 4) throw std::runtime_error("AnimationActionMap: truncated FORM type");
            validateChunks(bytes, start + 4, start + size, depth + 1);
        }
        start += size;
    }
}

const IffChunk& uniqueChild(const IffChunk& parent, uint32_t id, uint32_t formType = 0) {
    const IffChunk* found = nullptr;
    for (const auto& child : parent.children) {
        if (child.id != id || (id == kFormTag && child.formType != formType)) continue;
        if (found) throw std::runtime_error("AnimationActionMap: duplicate direct container/chunk");
        found = &child;
    }
    if (!found) throw std::runtime_error("AnimationActionMap: missing direct container/chunk");
    return *found;
}
} // namespace

AnimationActionMapData AnimationActionMap::parse(const std::vector<uint8_t>& bytes) {
    validateChunks(bytes, 0, bytes.size(), 0);
    const auto top = IffReader::parse(bytes);
    if (top.size() != 1 || top[0].id != kFormTag || top[0].formType != 0x41534854) {
        throw std::runtime_error("AnimationActionMap: expected FORM ASHT");
    }
    const auto& root = top[0];
    if (root.children.size() != 1 || root.children[0].id != kFormTag ||
        root.children[0].formType != 0x30303032) {
        throw std::runtime_error("AnimationActionMap: expected version 0002");
    }
    const auto& version = root.children[0];
    if (version.children.size() != 1 || version.children[0].id != kFormTag ||
        version.children[0].formType != kStat) {
        throw std::runtime_error("AnimationActionMap: expected one root STAT");
    }
    const auto& state = version.children[0];
    auto info = uniqueChild(state, kInfo).data;
    info.resetReadCursor();
    AnimationActionMapData result;
    result.rootStateName = readNulTerminatedString(info);
    result.rootAnimationStateName = readNulTerminatedString(info);
    if (result.rootStateName.empty() || info.remaining() != 0) {
        throw std::runtime_error("AnimationActionMap: invalid root STAT INFO");
    }
    const auto& actions = uniqueChild(state, kFormTag, kActs);
    auto countInfo = uniqueChild(actions, kInfo).data;
    countInfo.resetReadCursor();
    const auto declaredCount = countInfo.readUint16();
    if (countInfo.remaining() != 0) throw std::runtime_error("AnimationActionMap: invalid ACTS INFO");
    size_t actualCount = 0;
    for (const auto& child : actions.children) {
        if (child.id == kInfo) continue;
        ++actualCount;
        if (child.id == kFormTag && child.formType == kMvac) {
            ++result.skippedMultiActionCount;
            continue;
        }
        if (child.id != kActn) throw std::runtime_error("AnimationActionMap: unsupported ACTS entry");
        auto data = child.data;
        data.resetReadCursor();
        auto name = readNulTerminatedString(data);
        AnimationActionMapping mapping;
        mapping.animationStateName = readNulTerminatedString(data);
        if (name.empty() || mapping.animationStateName.empty() || data.remaining() != 2) {
            throw std::runtime_error("AnimationActionMap: invalid ACTN mapping");
        }
        mapping.opaqueMetadata = {data.readByte(), data.readByte()};
        if (!result.actions.emplace(std::move(name), std::move(mapping)).second) {
            throw std::runtime_error("AnimationActionMap: duplicate action mapping");
        }
    }
    if (actualCount != declaredCount) throw std::runtime_error("AnimationActionMap: ACTS count mismatch");
    return result;
}

} // namespace assets
