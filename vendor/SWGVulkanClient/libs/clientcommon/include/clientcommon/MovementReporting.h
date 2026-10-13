#pragma once
#include "worldmodel/SpatialTransform.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace clientcommon {
struct MovementFrame {
    uint64_t objectId=0,parentId=0;
    float x=0,y=0,z=0,yaw=0,speed=0;
    std::optional<worldmodel::Quaternion> orientation;
};
struct MovementParentFrame { float x=0,y=0,z=0,yaw=0; std::optional<worldmodel::Quaternion> rotation; };
struct MovementReport { MovementFrame frame; uint32_t timestamp=0,counter=0; std::vector<uint8_t> payload; };
// Frames are world-space. A nonzero parent requires its resolved building frame.
// No parented frame can fall back to a world-space packet.
std::optional<MovementFrame> movementWireFrame(const MovementFrame&,const std::optional<MovementParentFrame>&);
enum class CorrectionDecision {KeepPrediction,ApplyAuthoritative,IgnoreStaleSnapshot};
class SelfCorrectionTracker {
public:
    CorrectionDecision observe(uint32_t revision,uint32_t authoritativeGeneration,bool currentCounter,bool resolved);
private:
    uint32_t lastGeneration_=0;
    std::optional<uint32_t> ignoredRevision_;
};
class MovementReporter {
public:
    explicit MovementReporter(uint64_t originMs,uint32_t initialCounter=0):originMs_(originMs),lastSentMs_(originMs),counter_(initialCounter){}
    std::optional<MovementReport> poll(uint64_t nowMs,const MovementFrame&,const std::optional<MovementParentFrame>& = std::nullopt);
    bool correctionIsCurrent(uint32_t counter) const;
    void adoptCounter(uint32_t counter);
private:
    uint64_t originMs_,lastSentMs_;
    uint32_t counter_=0;
};
}
