#pragma once
#include "animation/SkeletalPose.h"

namespace animation {

// Bounded SPAT reconstruction for idle/walk/run. Timed transition constants,
// neighboring speed interpolation and normalized child clocks are recovered
// from the shipped executable. Ambient triggers/controller policy are separate.
class LocomotionPlayback {
public:
    explicit LocomotionPlayback(std::vector<assets::AnimationClipData> clips);
    assets::Float3 advance(float seconds, float requestedSpeed);
    std::vector<DirectX::XMMATRIX> sample(const assets::SkeletonData& skeleton) const;
    AnimationPose sampleAnimation(const assets::SkeletonData& skeleton) const;
    float walkingSpeed() const { return speeds_.at(1); }
    float runningSpeed() const { return speeds_.back(); }
    float movingWeight() const { return movingWeight_; }
    float idleFrame() const { return idleFrame_; }
    float frame(size_t child) const { return clips_.at(child).loopFrame(phase_*clips_.at(child).durationFrames); }
    float speed() const { return requestedSpeed_; }
    float upperWeight() const { return upperWeight_; }
    size_t lowerChild() const { return lower_; }
    size_t upperChild() const { return upper_; }
    const assets::AnimationClipData& clip(size_t child) const { return clips_.at(child); }
    void reset();
private:
    std::vector<assets::AnimationClipData> clips_;
    std::vector<float> speeds_,cycleLengths_;
    double phase_=0;
    float idleFrame_=0,movingWeight_=0,requestedSpeed_=0,upperWeight_=0;
    float transitionElapsed_=0,transitionStart_=0,transitionDuration_=0;
    bool moving_=false;
    size_t lower_=1,upper_=1;
};

// Rigid local-pose interpolation: quaternion SLERP and translation LERP.
// Common bind/pre/post rotations make blending composed local rotations
// equivalent to blending the original animation rotations before composition.
std::vector<DirectX::XMMATRIX> blendLocalPoses(const std::vector<DirectX::XMMATRIX>& a,
                                             const std::vector<DirectX::XMMATRIX>& b,float weight);
}
