#pragma once
#include "animation/LocomotionPlayback.h"
#include "assets/AnimationStateTable.h"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <limits>
namespace animation {
struct PriorityBlendRecord {
    int mode=2;
    float weight=1;
    void advance(float seconds,int actionPriority,int basePriority);
};
AnimationPose evaluatePriorityBlend(const AnimationPose& action,const AnimationPose& base,
                                   const std::vector<PriorityBlendRecord>& records);
struct AnimationPriorities { int transforms=0,locomotion=0; };
// AGAT arithmetic with caller-supplied eligibility and random integers. The
// game's shared RNG history and gameplay predicates are not synthesized here.
class AmbientActionGenerator {
public:
    using RandomInteger=std::function<uint32_t()>;
    AmbientActionGenerator(const assets::AnimationNode& variant,RandomInteger random);
    std::optional<std::string> advance(float seconds,bool eligible);
    float countdown() const {return countdown_;}
private:
    float sampleTimer();
    uint32_t randomInteger();
    RandomInteger random_;
    std::vector<std::pair<std::string,float>> cumulative_;
    float minimum_=0,maximum_=0,countdown_=0;
};
// Original action/loop priority combiner, one-shot replacement and nested
// .15s timed replacements. Uniform PUNF groups cover the packaged idle actions.
// Queued actions, additive facial track and gameplay state transitions remain
// separate controllers; this class implements the verified action/loop route.
class ActionPlayback {
public:
    ActionPlayback(assets::SkeletonData skeleton,std::vector<assets::AnimationClipData> baseClips,
                   std::vector<AnimationPriorities> basePriorities);
    void registerAction(std::string name,assets::AnimationClipData clip,AnimationPriorities priorities);
    bool playAction(const std::string& name);
    void clearAction();
    void setAmbient(const assets::AnimationNode& variant,AmbientActionGenerator::RandomInteger random);
    void setAmbientEligible(bool value) {ambientEligible_=value;}
    assets::Float3 advance(float seconds,float speed);
    AnimationPose sampleAnimation();
    std::vector<DirectX::XMMATRIX> sample(const assets::SkeletonData& skeleton);
    void reset();
    bool actionRouted() const {return actionRouted_;}
    bool actionActive() const {return bool(action_);}
    const std::string& actionName() const {return actionName_;}
    const std::vector<PriorityBlendRecord>& records() const {return records_;}
    float actionFrame() const;
    float replacementWeight() const;
    float walkingSpeed() const {return base_.walkingSpeed();}
    float runningSpeed() const {return base_.runningSpeed();}
    float movingWeight() const {return base_.movingWeight();}
    float idleFrame() const {return base_.idleFrame();}
    float frame(size_t child) const {return base_.frame(child);}
    const assets::AnimationClipData& clip(size_t child) const {return base_.clip(child);}
private:
    struct Definition {assets::AnimationClipData clip;std::vector<int> bindings;AnimationPriorities priorities;};
    struct Node {
        std::shared_ptr<const Definition> definition;
        std::shared_ptr<Node> previous,current;
        float elapsed=0,weight=0;
    };
    static float alterNode(std::shared_ptr<Node>& node,float seconds);
    static void advanceNode(std::shared_ptr<Node>& node,float seconds);
    AnimationPose sampleNode(const std::shared_ptr<Node>& node) const;
    static AnimationPriorities priorities(const std::shared_ptr<Node>& node);
    AnimationPriorities basePriorities() const;
    assets::SkeletonData skeleton_;
    LocomotionPlayback base_;
    std::vector<AnimationPriorities> basePriorities_;
    std::map<std::string,std::shared_ptr<const Definition>> definitions_;
    std::shared_ptr<Node> action_;
    AnimationPose retained_;
    std::vector<PriorityBlendRecord> records_;
    std::optional<AmbientActionGenerator> ambient_;
    bool ambientEligible_=false,actionRouted_=false;
    std::string actionName_;
};
}
