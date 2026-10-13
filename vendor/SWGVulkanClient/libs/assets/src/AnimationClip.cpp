#include "assets/AnimationClip.h"

#include <cctype>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "assets/IffReader.h"

namespace assets {

namespace {
constexpr uint32_t kCkatTag = 0x434B4154; // 'CKAT'
constexpr uint32_t kXfrmTag = 0x5846524D; // 'XFRM'
constexpr uint32_t kXfinTag = 0x5846494E; // 'XFIN'
constexpr uint32_t kArotTag = 0x41524F54; // 'AROT'
constexpr uint32_t kQchnTag = 0x5143484E; // 'QCHN'
constexpr uint32_t kAtrnTag = 0x4154524E; // 'ATRN'
constexpr uint32_t kChnlTag = 0x43484E4C; // 'CHNL'
constexpr uint32_t kLoctTag = 0x4C4F4354; // 'LOCT'
constexpr uint32_t kSrotTag = 0x53524F54; // 'SROT'
constexpr uint32_t kInfoTag = 0x494E464F; // 'INFO'

// Original SWGEmu.exe (SHA-256 58012e57...322b2a8), instructions
// 0x00ac0e20..0x00ac0ed5 initialize every context 0x80..0xfe.
// Static rows at 0x018af640 have 2^level entries and base step 2/(2^level+1).
// The old partial 0x9d..0xfd dump silently clamped real idle/walk channels.
// Keep the generator and reject invalid bytes instead of inventing a rotation.
struct BaseTableEntry { uint8_t byte; float base; int level; };
struct ScaleTableEntry { float scaleElevenBit; float scaleTenBit; };
constexpr ScaleTableEntry scaleForLevel(int level) {
    const double halfRange = 2.0 / (double((1u << level) + 1u));
    // 0xac0ba0 divides the original 4.0 constant by count+1;
    // 0xac0e3c then halves that interval before computing decode scales.
    // This is independent of the stored base-center step above.
    // These reciprocals are float32 constants in the original executable.
    return {float(halfRange * double(0.0009775171056389809f)),
            float(halfRange * double(0.001956947147846222f))};
}
constexpr ScaleTableEntry kScaleTable[7] = {
    scaleForLevel(0), scaleForLevel(1), scaleForLevel(2), scaleForLevel(3),
    scaleForLevel(4), scaleForLevel(5), scaleForLevel(6)};
BaseTableEntry lookupBaseTableEntry(uint8_t byte) {
    for (int level=0; level<=6; ++level) {
        const unsigned count=1u<<level, first=256u-2u*count;
        if (byte>=first && byte<first+count) {
            // The original base generator uses the stored float32 row step
            // in x87 arithmetic, rounding only the final base to float32.
            const float step=float(2.0 / double(count+1u));
            const float base=float(double(byte-first+1u)*double(step)-1.0);
            return {byte,base,level};
        }
    }
    throw std::runtime_error("AnimationClip: invalid quaternion context byte");
}

// Real per-keyframe rotation payload decode ("smallest three" quantized
// quaternion). Phase 21 live x32dbg reverse-engineering of the real,
// official SWGEmu.exe client (traced from real string landmarks in the
// binary all the way down to real FPU instructions around
// swgemu.exe:00AC0DE3-00AC0E0F) found the real client's own reconstruction
// of the dropped (4th, omitted) quaternion component: it computes
// `sqrt(1 - x*x - y*y - z*z)` via a bare `fsqrt` and stores it directly -
// UNCONDITIONALLY POSITIVE, no sign check, no candidate selection anywhere
// in the real decode path. This directly refutes this project's earlier
// (same phase, same session) assumption - built on general "smallest
// three"/Granny3D background research rather than the real client's own
// code - that the dropped component's sign is real, encoder-intended
// information requiring disambiguation. That wrong assumption had driven an
// entire two-state Viterbi DP pass (picking whichever of two candidate
// signs minimized total angular change across a channel) plus a
// freeze-on-anomaly mitigation for the cases the DP still got wrong - both
// now removed entirely. The real fix is to never guess a sign at all,
// matching the real client exactly: the encoder guarantees the dropped
// component's sign in advance (by negating the whole quaternion, valid
// since `q` and `-q` are the same rotation, whenever needed so the dropped
// component comes out non-negative), so the decoder never needs to.
int g_bitLayoutVariant = 2; // see AnimationClip::setBitLayoutVariantForTesting() - 2 is the confirmed-real layout

// Phase 21 live experiment: the Z-negation below was found and validated
// using LEG motion specifically (a real live A/B test on a real leg swing) -
// never independently verified for arms, which have been the single most
// consistently flagged bone group in every diagnostic tonight, and were
// confirmed live (via a real RenderDoc GPU capture) to be the very first
// submesh that renders malformed, before any other body part. Testing
// whether arms need a DIFFERENT real axis convention than legs/spine (not
// implausible for an old, hand-authored skeleton). true = skip the
// Z-negation for arm-chain bones only.
bool g_experimentSkipZNegationForArms = false;

// Phase 21 live experiment: `root` is architecturally unlike every other
// bone - it has no parent, so its decoded rotation directly IS the whole
// body's world orientation rather than one joint's contribution buried in a
// chain. Live evidence (self flipping/twisting during real WASD movement,
// worsening the longer the walk clip loops, but perfectly stable with
// animation disabled via the 'B' bind-pose debug key) isolates the problem
// to the walk clip's decoded rotation data specifically, and root is the
// one bone whose own error would visibly rotate the ENTIRE body rather than
// just one limb - the same Z-negation convention question already found
// real for arms, now being tested for root specifically. true = skip the
// Z-negation for the `root` bone only.
bool g_experimentSkipZNegationForRoot = false;

// Phase 21 live experiment (2026-07-25) - direct user report from a real
// walk cycle: the legs visibly CROSS during walking (one leg swinging
// toward/through the other's side) rather than staying on their own side -
// a real, distinctive signature of a lateral/handedness sign error, the
// same class of bug already found and fixed for the wrist/finger chain
// this session (Z-negation needed SKIPPED there, unlike every other
// bone). Testing the identical fix for the leg chain. true = skip the
// Z-negation for leg-chain bones only.
bool g_experimentSkipZNegationForLegs = false;

bool isArmChainBone(const std::string& boneName) {
    // Phase 21 live experiment: tried extending this to include finger
    // bones (thumb/index/ring), on the theory that they need the same
    // Z-negation treatment arms already confirmed they need - live result
    // was NEGATIVE: toggling it flipped the hand artifact's direction
    // rather than fixing it, meaning both "negate" and "don't negate" are
    // wrong for fingers, just wrong in different directions. Reverted -
    // fingers are NOT simply an extension of the arm-chain Z-negation
    // fix; whatever's actually wrong with them is something else. Kept
    // scoped to the originally-confirmed arm/forearm/ulna/wrist bones only.
    static const char* kArmBones[] = {"larm",     "lforearm", "lulna",    "lwrist",
                                       "rarm",     "rforearm", "rulna",    "rwrist"};
    for (const char* name : kArmBones) {
        if (boneName.size() == std::strlen(name)) {
            bool match = true;
            for (size_t i = 0; i < boneName.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(boneName[i])) != name[i]) {
                    match = false;
                    break;
                }
            }
            if (match) return true;
        }
    }
    return false;
}

bool isRootBone(const std::string& boneName) {
    static const char kRoot[] = "root";
    if (boneName.size() != std::strlen(kRoot)) return false;
    for (size_t i = 0; i < boneName.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(boneName[i])) != kRoot[i]) return false;
    }
    return true;
}

// Phase 21 live experiment: variant 2's "always drop w" rule (see
// decodeSmallestThreeQuaternion's own comment - no per-keyframe/per-channel
// dropped-axis index was found stored anywhere in the real client) fixed
// the whole-body orientation and most limbs live, but legs specifically
// still show real, live-confirmed artifacts (flat "sail" triangles,
// twisting) - suggesting legs may genuinely drop a DIFFERENT axis than w.
// -1 = disabled (use the normal always-w rule for every bone, including
// legs); 0/1/2/3 = force leg-chain bones specifically to drop that axis
// instead, to empirically find the real one without needing more RE.
int g_legForcedDropIndex = -1;

// Phase 21 live experiment: same idea as g_legForcedDropIndex, now for
// finger bones specifically. Extending the arm-chain Z-negation fix to
// fingers gave a NEGATIVE result (flipped the artifact's direction rather
// than fixing it - see isArmChainBone's own comment), which rules out a
// simple sign issue but doesn't rule out fingers needing a different
// FIXED dropped axis than the normal always-w rule, the same open
// question legs raised before the real base/scale tables were extracted.
// -1 = disabled (normal always-w rule); 0/1/2/3 = force finger bones to
// drop that axis instead.
int g_fingerForcedDropIndex = -1;

// Widened 2026-07-23: user confirmed live that the elbow-through-wrist
// bones show the exact same collapse artifact as the fingers, not a
// separate bug - despite lforearm/lulna/lwrist already being in
// isArmChainBone's Z-negation group above, that fix alone doesn't resolve
// them, the same way it didn't resolve fingers. Reusing this SAME override
// mechanism (rather than a separate one) so the existing 'G'/'H' live
// debug keys can A/B-test candidate fixes across the whole broken chain at
// once.
bool isFingerChainBone(const std::string& boneName) {
    static const char* kFingerBones[] = {"lthumb01", "lthumb02", "lindex01", "lindex02",
                                          "lring01",  "lring02",  "rthumb01", "rthumb02",
                                          "rindex01", "rindex02", "rring01",  "rring02",
                                          "lforearm", "lulna",    "lwrist",
                                          "rforearm", "rulna",    "rwrist"};
    for (const char* name : kFingerBones) {
        if (boneName.size() == std::strlen(name)) {
            bool match = true;
            for (size_t i = 0; i < boneName.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(boneName[i])) != name[i]) {
                    match = false;
                    break;
                }
            }
            if (match) return true;
        }
    }
    return false;
}

bool isLegChainBone(const std::string& boneName) {
    static const char* kLegBones[] = {"lthigh", "lshin", "lankle", "ltoe",
                                       "rthigh", "rshin", "rankle", "rtoe"};
    for (const char* name : kLegBones) {
        if (boneName.size() == std::strlen(name)) {
            bool match = true;
            for (size_t i = 0; i < boneName.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(boneName[i])) != name[i]) {
                    match = false;
                    break;
                }
            }
            if (match) return true;
        }
    }
    return false;
}

Quaternion decodeSmallestThreeQuaternion(uint32_t u32, bool skipZNegation, int forcedIdxOverride,
                                          uint8_t ctxByteA, uint8_t ctxByteB, uint8_t ctxByteC) {
    constexpr float kScale = 0.70710678f; // 1/sqrt(2) - max magnitude of a non-largest quaternion component
    uint32_t idx;
    uint32_t f0, f1, f2;
    float vals[3];
    double zExtended = 0.0;
    if (g_bitLayoutVariant == 2) {
        // SWGEmu.exe 0xac0d90: signed-magnitude x/y/z fields, 11/11/10
        // bits, per-channel quantizer contexts, with w reconstructed positive.
        // Diagnostic variants below are retained for older synthetic probes.
        idx = forcedIdxOverride >= 0 ? static_cast<uint32_t>(forcedIdxOverride) : 3u;
        uint32_t fieldA = (u32 >> 21) & 0x7FFu; // bits 31-21, 11 bits (1 sign + 10 magnitude)
        uint32_t fieldB = (u32 >> 10) & 0x7FFu; // bits 20-10, 11 bits (1 sign + 10 magnitude)
        uint32_t fieldC = u32 & 0x3FFu;         // bits 9-0, 10 bits (1 sign + 9 magnitude)
        auto decodeField11 = [](uint32_t field, uint8_t ctxByte) {
            BaseTableEntry entry = lookupBaseTableEntry(ctxByte);
            float scale = kScaleTable[entry.level].scaleElevenBit;
            bool negative = (field & 0x400u) != 0; // bit 10 (top bit of an 11-bit field)
            uint32_t magnitude = field & 0x3FFu;    // bits 0-9, max 1023
            double delta = static_cast<double>(magnitude) * scale;
            return negative ? (static_cast<double>(entry.base) - delta)
                            : (static_cast<double>(entry.base) + delta);
        };
        auto decodeField10 = [](uint32_t field, uint8_t ctxByte) {
            BaseTableEntry entry = lookupBaseTableEntry(ctxByte);
            float scale = kScaleTable[entry.level].scaleTenBit;
            bool negative = (field & 0x200u) != 0; // bit 9 (top bit of a 10-bit field)
            uint32_t magnitude = field & 0x1FFu;    // bits 0-8, max 511
            double delta = static_cast<double>(magnitude) * scale;
            return negative ? (static_cast<double>(entry.base) - delta)
                            : (static_cast<double>(entry.base) + delta);
        };
        vals[0] = static_cast<float>(decodeField11(fieldA, ctxByteA));
        vals[1] = static_cast<float>(decodeField11(fieldB, ctxByteB));
        // AC0DE8 stores a duplicate of Z, retaining the unrounded x87 value
        // for W's square root; X/Y are reloaded from their float32 stores.
        zExtended = decodeField10(fieldC, ctxByteC);
        vals[2] = static_cast<float>(zExtended);
    } else if (g_bitLayoutVariant == 1) {
        // Variant 1: idx in the bottom 2 bits, signed (two's complement)
        // fields - edged out variant 0 by ~0.01% aggregate real validity
        // across a 75k-keyframe, 57-file real sample (see
        // AnimationClip.h's own comment) - close enough that it needs a
        // real live visual A/B, not just the validity metric, to decide.
        idx = u32 & 0x3u;
        f0 = (u32 >> 2) & 0x3FFu;
        f1 = (u32 >> 12) & 0x3FFu;
        f2 = (u32 >> 22) & 0x3FFu;
        auto convSigned = [](uint32_t field) {
            int32_t signedField = field >= 512 ? static_cast<int32_t>(field) - 1024 : static_cast<int32_t>(field);
            return static_cast<float>(signedField) / 512.0f * kScale;
        };
        vals[0] = convSigned(f0);
        vals[1] = convSigned(f1);
        vals[2] = convSigned(f2);
    } else {
        idx = (u32 >> 30) & 0x3u;
        f0 = u32 & 0x3FFu;
        f1 = (u32 >> 10) & 0x3FFu;
        f2 = (u32 >> 20) & 0x3FFu;
        auto conv = [](uint32_t field) {
            return (static_cast<float>(field) - 511.5f) / 511.5f * kScale;
        };
        vals[0] = conv(f0);
        vals[1] = conv(f1);
        vals[2] = conv(f2);
    }
    float sumSq = vals[0] * vals[0] + vals[1] * vals[1] + vals[2] * vals[2];
    // The original uses bare fsqrt. Retain the native safety clamp for
    // malformed data; valid audited clips never require this fallback.
    float dropped;
    if (g_bitLayoutVariant == 2) {
        double residual = 1.0 - static_cast<double>(vals[0]) * vals[0]
                              - static_cast<double>(vals[1]) * vals[1]
                              - zExtended * zExtended;
        dropped = residual > 0.0 ? static_cast<float>(std::sqrt(residual)) : 0.0f;
    } else {
        dropped = sumSq < 1.0f ? std::sqrt(1.0f - sumSq) : 0.0f;
    }

    float comps[4];
    int src = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        comps[i] = (i == idx) ? dropped : vals[src++];
    }
    // The original CKAT decoder stores these components without normalization.
    // Keep normalization only for the older diagnostic layouts.
    float lenSq = comps[0] * comps[0] + comps[1] * comps[1] + comps[2] * comps[2] + comps[3] * comps[3];
    if (g_bitLayoutVariant != 2 && lenSq > 1e-8f) {
        float invLen = 1.0f / std::sqrt(lenSq);
        comps[0] *= invLen;
        comps[1] *= invLen;
        comps[2] *= invLen;
        comps[3] *= invLen;
    }
    // CKAT's production layout keeps the original axes unchanged.
    Quaternion q;
    q.x = comps[0];
    q.y = comps[1];
    q.z = skipZNegation ? comps[2] : -comps[2];
    q.w = comps[3];
    return q;
}

// Shared by parseQchn (animated keyframes) and the SROT static-rotation
// decode below - both real "smallest three" quantized payloads need the
// exact same per-bone-group skip/forced-idx decode overrides.
void computeZNegationAndForcedIdx(const std::string& boneName, bool& skipZNegation, int& forcedIdxOverride) {
    // The original 0x00ac0d90 decoder writes x/y/z without any axis flip.
    // Keep CKAT rotations in the same quaternion coordinates as .skt data;
    // per-bone sign guesses cannot be a consistent coordinate conversion.
    skipZNegation = g_bitLayoutVariant == 2 ||
                     (g_experimentSkipZNegationForArms && isArmChainBone(boneName)) ||
                     (g_experimentSkipZNegationForRoot && isRootBone(boneName)) ||
                     (g_experimentSkipZNegationForLegs && isLegChainBone(boneName)) ||
                     isFingerChainBone(boneName);
    forcedIdxOverride = -1;
    if (g_legForcedDropIndex >= 0 && isLegChainBone(boneName)) {
        forcedIdxOverride = g_legForcedDropIndex;
    } else if (g_fingerForcedDropIndex >= 0 && isFingerChainBone(boneName)) {
        forcedIdxOverride = g_fingerForcedDropIndex;
    }
}

// A real CHNK QCHN: leading uint16 keyframe count, then a real 3-byte
// per-channel value - confirmed live (x32dbg, 2026-07-22 morning session)
// to be 3 SEPARATE bytes, one per retained rotation component, each a
// real "context byte" selecting that component's base/level from
// kBaseTable above (see decodeSmallestThreeQuaternion's own comment) -
// then `count` real keyframe records, each [uint16 frame][uint32 LE
// quantized rotation payload, see decodeSmallestThreeQuaternion above].
std::vector<QuaternionKeyframe> parseQchn(const IffChunk& chunk, const std::string& boneNameForDebug) {
    soe::PacketBuffer buf = chunk.data;
    buf.resetReadCursor();
    uint16_t rawCount = buf.readUint16();
    std::vector<uint8_t> contextBytes = buf.readBytes(3);
    size_t count = rawCount;

    // Diagnostics report the full original context range.
    if (isFingerChainBone(boneNameForDebug)) {
        auto describe = [](uint8_t b) {
            return (b < 0x80 || b > 0xfe) ? "INVALID" : "valid";
        };
        std::printf("[ANIMDBG] QCHN context bytes bone=%s: A=0x%02x(%s) B=0x%02x(%s) C=0x%02x(%s) count=%u\n",
                    boneNameForDebug.c_str(), contextBytes[0], describe(contextBytes[0]), contextBytes[1],
                    describe(contextBytes[1]), contextBytes[2], describe(contextBytes[2]), rawCount);
    }

    // Real fix, live-verified 2026-07-25 (post quaternion-byte-order-fix
    // retest of the Phase 21 finger 'H' axis-fix debug key, previously
    // tested with a negative result under the OLD, byte-order-broken
    // skeleton parser - see project_animation_phase21_inprogress.md's "v3"
    // section, that old result no longer applies): the wrist/finger chain
    // (this project's own isFingerChainBone list, already widened to
    // include forearm/ulna/wrist) needs Z-negation SKIPPED, unlike every
    // other bone - direct live A/B against a live server confirmed fingers curl
    // the correct direction (into the palm, not away from it) only once
    // this is applied. Now the permanent default rather than a manual
    // debug-key toggle.
    bool skipZNegation;
    int forcedIdxOverride;
    computeZNegationAndForcedIdx(boneNameForDebug, skipZNegation, forcedIdxOverride);
    std::vector<QuaternionKeyframe> result(count);
    for (size_t i = 0; i < count; ++i) {
        result[i].frame = buf.readUint16();
        result[i].rotation = decodeSmallestThreeQuaternion(buf.readUint32(), skipZNegation, forcedIdxOverride,
                                                             contextBytes[0], contextBytes[1], contextBytes[2]);
    }
    // Phase 21 diagnostic (2026-07-24, code audit) - last keyframe's frame
    // number, not just the count, for chain bones - independent per-channel
    // looping (see sampleRotationChannel's own period = lastFrame+1) means
    // two channels with different total durations would slowly drift out of
    // phase with each other over time even if each is individually sane.
    if (isFingerChainBone(boneNameForDebug) && !result.empty()) {
        std::printf("[ANIMDBG] QCHN bone=%s count=%zu lastFrame=%u\n", boneNameForDebug.c_str(), count,
                    result.back().frame);
    }
    return result;
}

// One real CHNK SROT entry: 3 context bytes (same per-component
// base/level selectors as a QCHN channel's own 3-byte header) followed by
// one real uint32 quantized rotation payload - the SAME "smallest three"
// encoding as a single QCHN keyframe, just one constant value instead of a
// curve. Confirmed real byte layout (the `TAG_SROT` read loop):
// `xFormat, yFormat, zFormat, compressedRotationValue`
// per entry, no leading count (the real client instead trusts a
// `staticRotationCount` read earlier from CHNK INFO, which this project's
// own parser doesn't separately track) - reading until the chunk's own
// data is exhausted is equivalent since every entry is a fixed 7 bytes.
struct RawSrotEntry {
    uint8_t xFormat;
    uint8_t yFormat;
    uint8_t zFormat;
    uint32_t compressedRotationValue;
};

std::vector<RawSrotEntry> parseSrotRaw(const IffChunk& chunk) {
    soe::PacketBuffer buf = chunk.data;
    buf.resetReadCursor();
    std::vector<RawSrotEntry> result;
    while (buf.remaining() >= 7) {
        RawSrotEntry entry;
        entry.xFormat = buf.readByte();
        entry.yFormat = buf.readByte();
        entry.zFormat = buf.readByte();
        entry.compressedRotationValue = buf.readUint32();
        result.push_back(entry);
    }
    return result;
}

// A real CHNK CHNL: leading uint16 keyframe count (no base prefix, unlike
// QCHN), then `count` real keyframe records, each [uint16 frame][float32
// LE value] - confirmed via real per-axis curves that are smooth/monotonic
// when read this way (a real root-bone translation curve during a real
// prone-to-standing clip).
std::vector<ScalarKeyframe> parseChnl(const IffChunk& chunk) {
    soe::PacketBuffer buf = chunk.data;
    buf.resetReadCursor();
    uint16_t count = buf.readUint16();
    std::vector<ScalarKeyframe> result;
    result.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        ScalarKeyframe kf;
        kf.frame = buf.readUint16();
        kf.value = buf.readFloat();
        result.push_back(kf);
    }
    return result;
}

// A real CHNK LOCT: float32 averageTranslationSpeed, then uint16 keyCount,
// then `keyCount` real [uint16 frame][float32 x][float32 y][float32 z]
// records (a real, UNCOMPRESSED Vector per keyframe - confirmed real byte
// layout, 2026-07-25, not guessed from byte patterns).
void parseLoct(const IffChunk& chunk, AnimationClipData& result) {
    soe::PacketBuffer buf = chunk.data;
    buf.resetReadCursor();
    result.averageTranslationSpeed = buf.readFloat();
    uint16_t keyCount = buf.readUint16();
    result.locomotionTranslationKeys.reserve(keyCount);
    for (uint16_t i = 0; i < keyCount; ++i) {
        LocomotionKeyframe kf;
        kf.frame = buf.readUint16();
        kf.translation.x = buf.readFloat();
        kf.translation.y = buf.readFloat();
        kf.translation.z = buf.readFloat();
        result.locomotionTranslationKeys.push_back(kf);
    }
}

} // namespace

Float3 AnimationClipData::locomotionDelta(float startFrame, float elapsedFrames) const {
    if(!std::isfinite(startFrame)||!std::isfinite(elapsedFrames))
        throw std::runtime_error("AnimationClip: non-finite locomotion interval");
    if(locomotionTranslationKeys.size()<2)return {};
    const double duration=durationFrames>0?durationFrames:locomotionTranslationKeys.back().frame;
    if(duration<=0)return {};
    using Vector=std::array<double,3>;
    auto vector=[](const Float3& v)->Vector{return {v.x,v.y,v.z};};
    const auto first=vector(locomotionTranslationKeys.front().translation);
    const auto last=vector(locomotionTranslationKeys.back().translation);
    auto at=[&](double frame)->Vector {
        const double cycles=std::floor(frame/duration);
        const double localFrame=frame-cycles*duration;
        Vector position=first;
        for(size_t i=1;i<locomotionTranslationKeys.size();++i) {
            const auto& a=locomotionTranslationKeys[i-1];
            const auto& b=locomotionTranslationKeys[i];
            if(localFrame>b.frame){position=vector(b.translation);continue;}
            const double span=double(b.frame)-a.frame;
            const double t=span>0?std::clamp((localFrame-a.frame)/span,0.0,1.0):0.0;
            const auto av=vector(a.translation),bv=vector(b.translation);
            for(size_t axis=0;axis<3;++axis)position[axis]=av[axis]+(bv[axis]-av[axis])*t;
            break;
        }
        for(size_t axis=0;axis<3;++axis)position[axis]+=cycles*(last[axis]-first[axis])-first[axis];
        return position;
    };
    const auto begin=at(startFrame),end=at(double(startFrame)+elapsedFrames);
    return {float(end[0]-begin[0]),float(end[1]-begin[1]),float(end[2]-begin[2])};
}

float AnimationClipData::loopFrame(float elapsedFrames) const {
    if (!std::isfinite(elapsedFrames)) {
        throw std::runtime_error("AnimationClip: non-finite playback frame");
    }
    float duration = durationFrames;
    if (!std::isfinite(duration) || duration < 0.0f) {
        throw std::runtime_error("AnimationClip: invalid playback duration");
    }
    if (duration == 0.0f) {
        for (const auto& bone : bones) {
            if (!bone.rotationKeyframes.empty())
                duration = std::max(duration, static_cast<float>(bone.rotationKeyframes.back().frame));
            for (const auto& axis : bone.translationAxis)
                if (!axis.empty()) duration = std::max(duration, static_cast<float>(axis.back().frame));
        }
    }
    if (duration == 0.0f) return 0.0f;
    float frame = std::fmod(elapsedFrames, duration);
    if (frame < 0.0f) frame += duration;
    return frame;
}

AnimationClipData AnimationClip::parse(const std::vector<uint8_t>& bytes) {
    auto topLevel = IffReader::parse(bytes);
    if (topLevel.empty() || topLevel[0].id != kFormTag || topLevel[0].formType != kCkatTag) {
        throw std::runtime_error("AnimationClip::parse: not a FORM(CKAT)-rooted file");
    }
    const IffChunk& ckat = topLevel[0];
    if (ckat.children.empty() || ckat.children[0].id != kFormTag) {
        throw std::runtime_error("AnimationClip::parse: missing clip data FORM");
    }
    const IffChunk& clipForm = ckat.children[0];

    const IffChunk* xfrmForm = findFirstForm(clipForm, kXfrmTag);
    const IffChunk* arotForm = findFirstForm(clipForm, kArotTag);
    if (xfrmForm == nullptr || arotForm == nullptr) {
        throw std::runtime_error("AnimationClip::parse: missing XFRM or AROT form");
    }
    const IffChunk* atrnForm = findFirstForm(clipForm, kAtrnTag); // optional - not every real clip animates a translated bone
    const IffChunk* loctChunk = findFirstChunk(clipForm, kLoctTag); // optional - only real locomotion clips (walk/run) have one
    const IffChunk* srotChunk = findFirstChunk(clipForm, kSrotTag); // optional - not every real clip has any non-animated bones

    std::vector<const IffChunk*> qchnChunks;
    for (const IffChunk& child : arotForm->children) {
        if (child.id == kQchnTag) {
            qchnChunks.push_back(&child);
        }
    }
    std::vector<const IffChunk*> chnlChunks;
    if (atrnForm != nullptr) {
        for (const IffChunk& child : atrnForm->children) {
            if (child.id == kChnlTag) {
                chnlChunks.push_back(&child);
            }
        }
    }
    std::vector<RawSrotEntry> srotEntries;
    if (srotChunk != nullptr) {
        srotEntries = parseSrotRaw(*srotChunk);
    }
    std::vector<float> staticTranslationValues;
    const auto* strnChunk = findFirstChunk(clipForm, 0x5354524E); // STRN
    if (strnChunk != nullptr) {
        soe::PacketBuffer buf = strnChunk->data;
        buf.resetReadCursor();
        if (buf.remaining() % 4 != 0)
            throw std::runtime_error("AnimationClip::parse: incomplete STRN value");
        while (buf.remaining() != 0) {
            const float value = buf.readFloat();
            if (!std::isfinite(value))
                throw std::runtime_error("AnimationClip::parse: non-finite STRN value");
            staticTranslationValues.push_back(value);
        }
    }

    AnimationClipData result;
    if (const auto* info = findFirstChunk(clipForm, kInfoTag)) {
        soe::PacketBuffer buf = info->data;
        buf.resetReadCursor();
        if (buf.remaining() != 16)
            throw std::runtime_error("AnimationClip::parse: INFO must contain 16 bytes");
        result.framesPerSecond = buf.readFloat();
        result.durationFrames = static_cast<float>(buf.readUint16());
        const uint16_t transformCount = buf.readUint16();
        const uint16_t animatedRotationCount = buf.readUint16();
        const uint16_t staticRotationCount = buf.readUint16();
        const uint16_t animatedTranslationCount = buf.readUint16();
        const uint16_t staticTranslationCount = buf.readUint16();
        if (!std::isfinite(result.framesPerSecond) || result.framesPerSecond <= 0.0f)
            throw std::runtime_error("AnimationClip::parse: invalid INFO frame rate");
        size_t actualTransforms = 0;
        for (const auto& child : xfrmForm->children)
            if (child.id == kXfinTag) ++actualTransforms;
        if (transformCount != actualTransforms || animatedRotationCount != qchnChunks.size() ||
            staticRotationCount != srotEntries.size() || animatedTranslationCount != chnlChunks.size() ||
            staticTranslationCount != staticTranslationValues.size())
            throw std::runtime_error("AnimationClip::parse: INFO channel counts do not match chunks");
    }
    for (const IffChunk& xfinChunk : xfrmForm->children) {
        if (xfinChunk.id != kXfinTag) {
            continue;
        }
        soe::PacketBuffer buf = xfinChunk.data;
        buf.resetReadCursor();

        AnimationBoneChannel bone;
        bone.boneName = readNulTerminatedString(buf);

        // Real per-bone record (10 bytes, confirmed fixed-size across every
        // real XFIN checked this session, regardless of bone name length):
        // [byte hasRotation][uint16 rotationChannelIndex - index into
        // AROT's QCHN channels when hasRotation!=0, or index into CHNK
        // SROT's static-rotation array when hasRotation==0 - confirmed
        // 2026-07-28 real field semantics; previously (wrongly) assumed
        // unused in the hasRotation==0 case]
        // [byte hasTranslation][uint16 x3 - real ATRN channel indices when
        // hasTranslation != 0 (confirmed only for `root` this session); a
        // real, separate per-bone counter (likely indexing STRN's unread
        // scale data) when hasTranslation==0].
        uint8_t hasRotation = buf.readByte();
        uint16_t rotationChannelIndex = buf.readUint16();
        uint8_t hasTranslation = buf.readByte();
        uint16_t translationChannelIndex[3];
        translationChannelIndex[0] = buf.readUint16();
        translationChannelIndex[1] = buf.readUint16();
        translationChannelIndex[2] = buf.readUint16();

        // Phase 21 diagnostic (2026-07-24, code audit) - full XFIN record
        // for chain bones, including hasTranslation - previously assumed
        // real translation channels are root-only, never directly checked
        // for this specific chain. A wrongly-applied translation on a
        // mid-chain bone would displace everything below it and compound
        // exactly like the observed symptom.
        if (isFingerChainBone(bone.boneName)) {
            std::printf(
                "[ANIMDBG] XFIN bone=%s hasRotation=%u rotIdx=%u hasTranslation=0x%02x transIdx=(%u,%u,%u)\n",
                bone.boneName.c_str(), hasRotation, rotationChannelIndex, hasTranslation,
                translationChannelIndex[0], translationChannelIndex[1], translationChannelIndex[2]);
        }
        if (hasRotation != 0 && rotationChannelIndex < qchnChunks.size()) {
            bone.rotationKeyframes = parseQchn(*qchnChunks[rotationChannelIndex], bone.boneName);
        } else if (hasRotation == 0 && rotationChannelIndex < srotEntries.size()) {
            // Real per-clip STATIC rotation - see AnimationBoneChannel's
            // own `hasStaticRotation` comment. NOT identity/no-delta in
            // general; a genuine, real, per-bone rest offset authored for
            // this specific clip, decoded with the exact same "smallest
            // three" math already confirmed correct for QCHN keyframes.
            const RawSrotEntry& entry = srotEntries[rotationChannelIndex];
            bool skipZNegation;
            int forcedIdxOverride;
            computeZNegationAndForcedIdx(bone.boneName, skipZNegation, forcedIdxOverride);
            bone.staticRotation = decodeSmallestThreeQuaternion(
                entry.compressedRotationValue, skipZNegation, forcedIdxOverride, entry.xFormat, entry.yFormat,
                entry.zFormat);
            bone.hasStaticRotation = true;
        }
        // Real bug found live (Phase 21): `hasTranslation` isn't a plain
        // boolean - it's a real per-axis bitmask (bit 3 = X, bit 4 = Y,
        // bit 5 = Z), confirmed via two real samples: 0x38 (0b00111000,
        // bits 3/4/5 all set) on a real clip whose root bone had all 3
        // real translation axes populated, vs 0x10 (0b00010000, bit 4
        // only) on a real walk clip whose root only has a real Y-axis
        // channel (its other two `translationChannelIndex` entries are
        // real but unused padding for the unset axes, not valid CHNL
        // indices - relying on bounds-checking alone to skip them would
        // work by accident in this file but not in general).
        for (int axis = 0; axis < 3; ++axis) {
            bool axisHasTranslation = (hasTranslation & (1u << (3 + axis))) != 0;
            uint16_t chnlIdx = translationChannelIndex[axis];
            if (!axisHasTranslation) {
                if (chnlIdx < staticTranslationValues.size()) {
                    bone.hasStaticTranslation[axis] = true;
                    const float value = staticTranslationValues[chnlIdx];
                    if (axis == 0) bone.staticTranslation.x = value;
                    else if (axis == 1) bone.staticTranslation.y = value;
                    else bone.staticTranslation.z = value;
                }
            } else if (chnlIdx < chnlChunks.size()) {
                bone.translationAxis[axis] = parseChnl(*chnlChunks[chnlIdx]);
            }
        }
        result.bones.push_back(std::move(bone));
    }
    if (loctChunk != nullptr) {
        parseLoct(*loctChunk, result);
    }
    return result;
}

void AnimationClip::setBitLayoutVariantForTesting(int variant) {
    g_bitLayoutVariant = variant;
}

void AnimationClip::setSkipZNegationForArmsForTesting(bool skip) {
    g_experimentSkipZNegationForArms = skip;
}

void AnimationClip::setSkipZNegationForRootForTesting(bool skip) {
    g_experimentSkipZNegationForRoot = skip;
}

void AnimationClip::setSkipZNegationForLegsForTesting(bool skip) {
    g_experimentSkipZNegationForLegs = skip;
}

void AnimationClip::setLegForcedDropIndexForTesting(int forcedIdx) {
    g_legForcedDropIndex = forcedIdx;
}

void AnimationClip::setFingerForcedDropIndexForTesting(int forcedIdx) {
    g_fingerForcedDropIndex = forcedIdx;
}

} // namespace assets
