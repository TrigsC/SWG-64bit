#include "worldmodel/SpatialTransform.h"
#include <cmath>
namespace worldmodel {
namespace {bool finite(assets::Float3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}}
std::optional<Quaternion> normalizedRotation(Quaternion q) {
    if(!std::isfinite(q.x)||!std::isfinite(q.y)||!std::isfinite(q.z)||!std::isfinite(q.w))return {};
    double length=std::sqrt(double(q.x)*q.x+double(q.y)*q.y+double(q.z)*q.z+double(q.w)*q.w);
    if(length<1e-10)return {};
    return Quaternion{float(q.x/length),float(q.y/length),float(q.z/length),float(q.w/length)};
}
Quaternion headingRotation(float yaw){return {0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};}
Quaternion inverseRotation(Quaternion q){return {-q.x,-q.y,-q.z,q.w};}
Quaternion multiplyRotation(Quaternion a,Quaternion b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
assets::Float3 rotateVector(Quaternion q,assets::Float3 p) {
    // v + 2*w*(q.xyz cross v) + 2*(q.xyz cross (q.xyz cross v)).
    double tx=2*(double(q.y)*p.z-double(q.z)*p.y),ty=2*(double(q.z)*p.x-double(q.x)*p.z),tz=2*(double(q.x)*p.y-double(q.y)*p.x);
    return {float(p.x+q.w*tx+q.y*tz-q.z*ty),float(p.y+q.w*ty+q.z*tx-q.x*tz),float(p.z+q.w*tz+q.x*ty-q.y*tx)};
}
float rotationHeading(Quaternion q){auto forward=rotateVector(q,{0,0,1});return std::atan2(forward.x,forward.z);}
assets::Float3 SpatialTransform::toWorld(assets::Float3 p)const {
    auto rotated=rotateVector(rotation,p);return {origin.x+rotated.x,origin.y+rotated.y,origin.z+rotated.z};
}
assets::Float3 SpatialTransform::toLocal(assets::Float3 p)const {
    return rotateVector(inverseRotation(rotation),{p.x-origin.x,p.y-origin.y,p.z-origin.z});
}
bool SpatialTransform::preservesWorldUp(float tolerance)const {
    auto up=rotateVector(rotation,{0,1,0});return std::hypot(up.x,up.z)<=tolerance&&std::abs(up.y-1)<=tolerance;
}
std::optional<Quaternion> objectRotation(const WorldObject& obj) {
    if(obj.fullRotationCurrent)
        return normalizedRotation({obj.quatX,obj.quatY,obj.quatZ,obj.quatW});
    return normalizedRotation(headingRotation(float(obj.direction)*6.2831853071795864769f/100.f));
}
std::optional<SpatialTransform> objectLocalTransform(const WorldObject& obj) {
    if(!obj.transformMessagesSeen||!finite({obj.x,obj.y,obj.z}))return {};
    auto q=objectRotation(obj);if(!q)return {};
    return SpatialTransform{{obj.x,obj.y,obj.z},*q};
}
std::optional<SpatialTransform> resolveWorldTransform(const std::unordered_map<uint64_t,WorldObject>& snapshot,const WorldObject& obj) {
    auto local=objectLocalTransform(obj);if(!local)return {};
    uint64_t parentId=obj.parentId?obj.parentId:(obj.spatialParentKnown?0:obj.containerId);
    if(!parentId)return local;
    auto cell=snapshot.find(parentId);
    if(cell==snapshot.end()||cell->second.typeTag!=ObjectTypeTag::Cell||!cell->second.containmentMessagesSeen||!cell->second.containerId)return {};
    auto building=snapshot.find(cell->second.containerId);
    if(building==snapshot.end()||building->second.parentId||building->second.containerId)return {};
    auto parent=objectLocalTransform(building->second);if(!parent)return {};
    auto rotation=normalizedRotation(multiplyRotation(parent->rotation,local->rotation));if(!rotation)return {};
    auto position=parent->toWorld(local->origin);if(!finite(position))return {};
    return SpatialTransform{position,*rotation};
}
} // namespace worldmodel
