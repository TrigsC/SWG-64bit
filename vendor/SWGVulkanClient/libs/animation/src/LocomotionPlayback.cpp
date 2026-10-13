#include "animation/LocomotionPlayback.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace animation {
using namespace DirectX;
std::vector<XMMATRIX> blendLocalPoses(const std::vector<XMMATRIX>& a,const std::vector<XMMATRIX>& b,float weight) {
    if(a.size()!=b.size()||!std::isfinite(weight)||weight<0||weight>1)
        throw std::runtime_error("Invalid local-pose blend");
    if(weight==0)return a;
    if(weight==1)return b;
    std::vector<XMMATRIX> result;result.reserve(a.size());
    for(size_t i=0;i<a.size();++i) {
        const auto rotation=XMQuaternionSlerp(XMQuaternionRotationMatrix(a[i]),XMQuaternionRotationMatrix(b[i]),weight);
        auto matrix=XMMatrixRotationQuaternion(XMQuaternionNormalize(rotation));
        matrix.r[3]=XMVectorLerp(a[i].r[3],b[i].r[3],weight);
        result.push_back(matrix);
    }
    return result;
}

LocomotionPlayback::LocomotionPlayback(std::vector<assets::AnimationClipData> clips):clips_(std::move(clips)) {
    if(clips_.size()<2)throw std::runtime_error("Speed graph requires idle and moving children");
    for(const auto& clip:clips_) {
        if(clip.durationFrames<=0||clip.framesPerSecond<=0)throw std::runtime_error("Invalid speed-child duration");
        const auto cycle=clip.locomotionDelta(0,clip.durationFrames);
        const float length=std::sqrt(cycle.x*cycle.x+cycle.y*cycle.y+cycle.z*cycle.z);
        cycleLengths_.push_back(length);
        speeds_.push_back(length*clip.framesPerSecond/clip.durationFrames);
    }
    if(speeds_[0]!=0)throw std::runtime_error("First speed child must be stationary");
    for(size_t i=1;i<speeds_.size();++i)
        if(speeds_[i]<=0||(i>1&&speeds_[i]<=speeds_[i-1]))throw std::runtime_error("Expected increasing authored moving speeds");
}
void LocomotionPlayback::reset() {
    phase_=idleFrame_=movingWeight_=requestedSpeed_=upperWeight_=0;
    transitionElapsed_=transitionStart_=transitionDuration_=0;
    moving_=false;lower_=upper_=1;
}
assets::Float3 LocomotionPlayback::advance(float seconds,float speed) {
    if(!std::isfinite(seconds)||seconds<0||!std::isfinite(speed)||speed<0)
        throw std::runtime_error("Invalid locomotion step");
    const bool moving=speed>0;
    if(moving!=moving_) {
        moving_=moving;transitionStart_=movingWeight_;transitionElapsed_=0;
        transitionDuration_=moving?.25f:.30f; // SWGEmu.exe 0x83b3ee / 0x83b416.
    }
    requestedSpeed_=speed;
    if(transitionDuration_>0) {
        transitionElapsed_=std::min(transitionDuration_,transitionElapsed_+seconds);
        const float target=moving_?1.f:0.f;
        movingWeight_=transitionStart_+(target-transitionStart_)*(transitionElapsed_/transitionDuration_);
    }
    if(!moving_) {
        idleFrame_=clips_[0].loopFrame(idleFrame_+seconds*clips_[0].framesPerSecond);
        return {}; // Ground/controller stop; outgoing moving pose is frozen.
    }
    lower_=upper_=1;upperWeight_=0;
    if(speed>=speeds_.back())lower_=upper_=clips_.size()-1;
    else for(size_t i=1;i+1<speeds_.size();++i) {
        if(speed>=speeds_[i]&&speed<speeds_[i+1]) {
            lower_=i;upper_=i+1;
            upperWeight_=(speed-speeds_[i])/(speeds_[i+1]-speeds_[i]);break;
        }
    }
    const float length=cycleLengths_[lower_]+(cycleLengths_[upper_]-cycleLengths_[lower_])*upperWeight_;
    const double elapsedCycles=(speed==speeds_[lower_]&&upperWeight_==0)
        ?double(seconds*clips_[lower_].framesPerSecond)/clips_[lower_].durationFrames
        :double(seconds)*speed/length;
    const auto a=clips_[lower_].locomotionDelta(phase_*clips_[lower_].durationFrames,elapsedCycles*clips_[lower_].durationFrames);
    const auto b=clips_[upper_].locomotionDelta(phase_*clips_[upper_].durationFrames,elapsedCycles*clips_[upper_].durationFrames);
    phase_=std::fmod(phase_+elapsedCycles,1.f);
    // Offline authored-motion policy: integrate the blended X/Z displacement.
    // The original gameplay controller's use of this delta remains separate.
    return {movingWeight_*(a.x+(b.x-a.x)*upperWeight_),movingWeight_*(a.y+(b.y-a.y)*upperWeight_),
            movingWeight_*(a.z+(b.z-a.z)*upperWeight_)};
}
AnimationPose LocomotionPlayback::sampleAnimation(const assets::SkeletonData& skeleton) const {
    auto pose=[&](size_t child,float frame) {
        return sampleAnimationPose(skeleton,&clips_[child],bindClipBoneIndices(skeleton,clips_[child]),frame);
    };
    auto idle=pose(0,idleFrame_);
    if(movingWeight_==0)return idle;
    auto moving=pose(lower_,frame(lower_));
    if(upper_!=lower_&&upperWeight_>0)moving=blendAnimationTransforms(moving,pose(upper_,frame(upper_)),upperWeight_);
    return blendAnimationTransforms(idle,moving,movingWeight_);
}
std::vector<XMMATRIX> LocomotionPlayback::sample(const assets::SkeletonData& skeleton) const {
    return composeAnimationPose(skeleton,sampleAnimation(skeleton));
}
}
