#include "clientcommon/RemoteMotion.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace clientcommon {
namespace {
float angle(float yaw) { return std::remainder(yaw,2*std::numbers::pi_v<float>); }
bool finite(const RemotePose& p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::isfinite(p.yaw); }
double distanceSquared(const RemotePose& a,const RemotePose& b) {
    const double x=double(a.x)-b.x,y=double(a.y)-b.y,z=double(a.z)-b.z;return x*x+y*y+z*z;
}
}
std::optional<RemotePose> RemoteMotion::sample(uint64_t now) const {
    if(!initialized_)return std::nullopt;
    if(!duration_||(now>=start_&&now-start_>=duration_))return to_;
    const double t=now<=start_?0.0:double(now-start_)/double(duration_);
    auto lerp=[&](float a,float b){return float(double(a)+(double(b)-a)*t);};
    return RemotePose{lerp(from_.x,to_.x),lerp(from_.y,to_.y),lerp(from_.z,to_.z),
        angle(from_.yaw+angle(to_.yaw-from_.yaw)*float(t))};
}
RemoteMotionUpdate RemoteMotion::update(const RemoteMotionSample& input,uint64_t now) {
    if(!finite(input.pose)||input.receivedMs>now)return RemoteMotionUpdate::Rejected;
    auto pose=input.pose;pose.yaw=angle(pose.yaw);
    if(!initialized_) {
        initialized_=true;from_=to_=pose;parent_=input.parentId;received_=input.receivedMs;
        start_=now;duration_=0;counter_=input.counter;return RemoteMotionUpdate::Initial;
    }
    const auto delta=input.counter-counter_;
    if(delta>=0x80000000u||input.receivedMs<received_)return RemoteMotionUpdate::Rejected;
    // A duplicate cannot rewrite a newer target, but a same-counter explicit
    // authoritative correction may legitimately replace its position.
    if(delta==0&&!input.authoritative)return RemoteMotionUpdate::Rejected;
    const auto interval=std::clamp<uint64_t>(input.receivedMs-received_,200,800);
    const bool snap=input.authoritative||input.parentId!=parent_||distanceSquared(to_,pose)>32.0*32.0;
    received_=input.receivedMs;counter_=input.counter;parent_=input.parentId;
    if(snap) {
        from_=to_=pose;start_=now;duration_=0;return RemoteMotionUpdate::Snapped;
    }
    if(distanceSquared(to_,pose)<1e-12&&std::abs(angle(to_.yaw-pose.yaw))<1e-6f)
        return RemoteMotionUpdate::Unchanged; // repeated stops never extend settling
    from_=*sample(now);to_=pose;start_=now;duration_=interval;
    return RemoteMotionUpdate::Retargeted;
}
}
