#pragma once
#include <cstdint>
#include <optional>

namespace clientcommon {
struct RemotePose { float x=0,y=0,z=0,yaw=0; };
struct RemoteMotionSample {
    RemotePose pose;
    uint64_t parentId=0,receivedMs=0;
    uint32_t counter=0;
    bool authoritative=false;
};
enum class RemoteMotionUpdate { Rejected,Initial,Retargeted,Unchanged,Snapped };
// Presentation only: continuous retargeting over the measured arrival interval
// (200..800 ms), no extrapolation/overshoot and exact final endpoints. Samples
// are resolved world coordinates. The server object model remains unchanged.
class RemoteMotion {
public:
    RemoteMotionUpdate update(const RemoteMotionSample&,uint64_t nowMs);
    std::optional<RemotePose> sample(uint64_t nowMs) const;
private:
    bool initialized_=false;
    RemotePose from_,to_;
    uint64_t parent_=0,received_=0,start_=0,duration_=0;
    uint32_t counter_=0;
};
}
