#include "clientcommon/MovementReporting.h"
#include "swgproto/DataTransform.h"
#include "swgproto/DataTransformWithParent.h"
#include <cmath>
namespace clientcommon {
std::optional<MovementFrame> movementWireFrame(const MovementFrame& world,const std::optional<MovementParentFrame>& parent) {
    auto valid=[](float v){return std::isfinite(v);};
    if(!world.objectId||!valid(world.x)||!valid(world.y)||!valid(world.z)||!valid(world.yaw)||!valid(world.speed)||world.speed<0||world.speed>50)return std::nullopt;
    auto local=world;
    auto orientation=worldmodel::normalizedRotation(world.orientation.value_or(worldmodel::headingRotation(world.yaw)));
    if(!orientation)return std::nullopt;
    if(world.parentId) {
        if(!parent||!valid(world.yaw-parent->yaw)||!valid(parent->x)||!valid(parent->y)||!valid(parent->z)||!valid(parent->yaw))return std::nullopt;
        auto rotation=worldmodel::normalizedRotation(parent->rotation.value_or(worldmodel::headingRotation(parent->yaw)));
        if(!rotation)return std::nullopt;
        worldmodel::SpatialTransform transform{{parent->x,parent->y,parent->z},*rotation};
        auto point=transform.toLocal({world.x,world.y,world.z});local.x=point.x;local.y=point.y;local.z=point.z;
        orientation=worldmodel::normalizedRotation(worldmodel::multiplyRotation(worldmodel::inverseRotation(*rotation),*orientation));
    }
    if(!orientation)return std::nullopt;
    local.orientation=*orientation;local.yaw=worldmodel::rotationHeading(*orientation);
    if(!valid(local.x)||!valid(local.y)||!valid(local.z)||!valid(local.yaw))return std::nullopt;
    const float limit=world.parentId?1024.f:8192.f;
    if(std::abs(local.x)>limit||std::abs(local.y)>limit||std::abs(local.z)>limit)return std::nullopt;
    return local;
}
CorrectionDecision SelfCorrectionTracker::observe(uint32_t revision,uint32_t generation,bool current,bool resolved) {
    if(generation!=lastGeneration_) {
        if(!current) {lastGeneration_=generation;ignoredRevision_=revision;}
        else if(resolved) {lastGeneration_=generation;ignoredRevision_.reset();return CorrectionDecision::ApplyAuthoritative;}
    }
    if(ignoredRevision_&&*ignoredRevision_==revision)return CorrectionDecision::IgnoreStaleSnapshot;
    ignoredRevision_.reset();return CorrectionDecision::KeepPrediction;
}
bool MovementReporter::correctionIsCurrent(uint32_t value) const {return (value-counter_)<0x80000000u;}
void MovementReporter::adoptCounter(uint32_t value) {if(correctionIsCurrent(value))counter_=value;}
std::optional<MovementReport> MovementReporter::poll(uint64_t nowMs,const MovementFrame& frame,const std::optional<MovementParentFrame>& parent) {
    // Fixed bounded cadence includes zero-speed stops, short taps and idle sync.
    // No catch-up burst after a render stall. Timestamp arithmetic wraps on wire.
    if(nowMs<lastSentMs_||nowMs-lastSentMs_<400)return std::nullopt;
    auto wire=movementWireFrame(frame,parent);if(!wire)return std::nullopt;
    const auto stamp=static_cast<uint32_t>(nowMs-originMs_);const auto count=counter_+1;
    const auto q=*wire->orientation;
    auto bytes=wire->parentId?swgproto::buildDataTransformWithParent(wire->objectId,stamp,count,wire->parentId,q.x,q.y,q.z,q.w,wire->x,wire->y,wire->z,wire->speed):swgproto::buildDataTransform(wire->objectId,stamp,count,q.x,q.y,q.z,q.w,wire->x,wire->y,wire->z,wire->speed);
    counter_=count;lastSentMs_=nowMs;
    return MovementReport{*wire,stamp,count,std::move(bytes)};
}
}
