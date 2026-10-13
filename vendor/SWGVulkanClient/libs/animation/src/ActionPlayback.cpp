#include "animation/ActionPlayback.h"
#include <algorithm>
#include <cmath>
#include <climits>
#include <stdexcept>
namespace animation {
namespace {
void checkStep(float seconds) {
    if(!std::isfinite(seconds)||seconds<0)throw std::runtime_error("Invalid action timestep");
}
constexpr float replacementDuration=.15f;
AnimationPriorities maximum(AnimationPriorities a,AnimationPriorities b) {
    return {std::max(a.transforms,b.transforms),std::max(a.locomotion,b.locomotion)};
}
}
AnimationTransform blendAnimationTransforms(const AnimationTransform& a,const AnimationTransform& b,float weight) {
    if(!std::isfinite(weight)||weight<0||weight>1)throw std::runtime_error("Invalid resolver blend weight");
    if(weight==0)return a;if(weight==1)return b;
    // Original shortest-path SLERP threshold and unnormalized resolver output.
    const auto& p=a.rotation;const auto& q=b.rotation;
    double dot=double(p.x)*q.x+double(p.y)*q.y+double(p.z)*q.z+double(p.w)*q.w;
    const double sign=dot<0?-1:1;dot=std::abs(dot);
    double first=1-double(weight),second=weight;
    if(1-dot>double(1.1920899822825959e-7f)) {
        const double theta=std::acos(std::clamp(dot,-1.0,1.0)),sine=std::sin(theta);
        first=std::sin((1-double(weight))*theta)/sine;second=std::sin(double(weight)*theta)/sine;
    }
    second*=sign;
    AnimationTransform result;
    result.rotation={float(first*p.x+second*q.x),float(first*p.y+second*q.y),
                     float(first*p.z+second*q.z),float(first*p.w+second*q.w)};
    const float complement=1-weight;
    result.translation={float(complement*a.translation.x)+float(weight*b.translation.x),
                        float(complement*a.translation.y)+float(weight*b.translation.y),
                        float(complement*a.translation.z)+float(weight*b.translation.z)};
    return result;
}
AnimationPose blendAnimationTransforms(const AnimationPose& a,const AnimationPose& b,float weight) {
    if(a.size()!=b.size())throw std::runtime_error("Resolver blend size mismatch");
    AnimationPose result;result.reserve(a.size());
    for(size_t i=0;i<a.size();++i)result.push_back(blendAnimationTransforms(a[i],b[i],weight));
    return result;
}
void PriorityBlendRecord::advance(float seconds,int p0,int p1) {
    checkStep(seconds);
    if(mode<0||mode>3||!std::isfinite(weight)||weight<0||weight>1)throw std::runtime_error("Invalid priority record");
    const bool actionWins=p0>=p1;
    if((mode==0&&actionWins)||(mode==2&&!actionWins))return;
    mode=actionWins?3:1;
    // Compare the held result before rounding to float, as x87 FST does.
    const double held=double(weight)+(actionWins?-4.0:4.0)*double(seconds);
    weight=float(held);
    if(!actionWins&&held>=1){weight=1;mode=2;}
    else if(actionWins&&held<=0){weight=0;mode=0;}
}
AnimationPose evaluatePriorityBlend(const AnimationPose& action,const AnimationPose& base,
                                   const std::vector<PriorityBlendRecord>& records) {
    if(action.size()!=base.size()||records.size()!=action.size()+1)throw std::runtime_error("Priority pose size mismatch");
    AnimationPose result;result.reserve(action.size());
    for(size_t i=0;i<action.size();++i) {
        const auto& record=records[i];
        if(record.mode==0)result.push_back(action[i]);
        else if(record.mode==2)result.push_back(base[i]);
        else if(record.mode==1||record.mode==3)result.push_back(blendAnimationTransforms(action[i],base[i],record.weight));
        else throw std::runtime_error("Invalid priority mode");
    }
    return result;
}
AmbientActionGenerator::AmbientActionGenerator(const assets::AnimationNode& variant,RandomInteger random)
    :random_(std::move(random)),minimum_(variant.minDurationSeconds),maximum_(variant.maxDurationSeconds) {
    if(variant.kind!=assets::AnimationNodeKind::Variant||!random_||!std::isfinite(minimum_)||
       !std::isfinite(maximum_)||minimum_<0||maximum_<minimum_)throw std::runtime_error("Invalid ambient generator");
    float total=0;for(const auto& entry:variant.actionTriggers) {
        if(!std::isfinite(entry.weight)||entry.weight<0)throw std::runtime_error("Unsupported negative/nonfinite ambient weight");
        total+=entry.weight;
    }
    const float scale=total>0?100/total:100;float cumulative=0;
    for(const auto& entry:variant.actionTriggers) {cumulative+=entry.weight;cumulative_.emplace_back(entry.name,cumulative*scale);}
    countdown_=sampleTimer();
}
uint32_t AmbientActionGenerator::randomInteger() {
    const auto value=random_();if(value>0x7fffffffU)throw std::runtime_error("Ambient random integer outside original range");return value;
}
float AmbientActionGenerator::sampleTimer() {
    return float(double(minimum_)+double(randomInteger())*double(maximum_-minimum_)*double(0x1p-31f));
}
std::optional<std::string> AmbientActionGenerator::advance(float seconds,bool eligible) {
    checkStep(seconds);const double held=double(countdown_)-seconds;countdown_=float(held);
    if(held>0)return {};
    std::optional<std::string> selected;
    if(eligible&&!cumulative_.empty()) {
        const float sample=float(double(randomInteger())*double(100.f*0x1p-31f));
        for(const auto& [name,threshold]:cumulative_)if(threshold>=sample){selected=name;break;}
    }
    countdown_=sampleTimer();return selected;
}
ActionPlayback::ActionPlayback(assets::SkeletonData skeleton,std::vector<assets::AnimationClipData> clips,
                               std::vector<AnimationPriorities> basePriorities)
    :skeleton_(std::move(skeleton)),base_(std::move(clips)),basePriorities_(std::move(basePriorities)),
     retained_(skeleton_.bones.size()),records_(skeleton_.bones.size()+1) {
    if(basePriorities_.size()<2)throw std::runtime_error("Missing base priorities");
    // Validate count against the actual speed graph without another accessor.
    for(size_t i=0;i<basePriorities_.size();++i)(void)base_.clip(i);
}
void ActionPlayback::registerAction(std::string name,assets::AnimationClipData clip,AnimationPriorities p) {
    if(name.empty()||clip.framesPerSecond<=0||clip.durationFrames<=0)throw std::runtime_error("Invalid action definition");
    auto definition=std::make_shared<Definition>();definition->bindings=bindClipBoneIndices(skeleton_,clip);
    definition->clip=std::move(clip);definition->priorities=p;definitions_[std::move(name)]=std::move(definition);
}
bool ActionPlayback::playAction(const std::string& name) {
    const auto found=definitions_.find(name);if(found==definitions_.end())return false;
    auto next=std::make_shared<Node>();next->definition=found->second;
    if(action_) {auto blend=std::make_shared<Node>();blend->previous=action_;blend->current=next;action_=blend;}
    else action_=next;
    actionName_=name;actionRouted_=true;return true;
}
void ActionPlayback::clearAction() {action_.reset();}
void ActionPlayback::setAmbient(const assets::AnimationNode& variant,AmbientActionGenerator::RandomInteger random) {
    ambient_.emplace(variant,std::move(random));
}
AnimationPriorities ActionPlayback::priorities(const std::shared_ptr<Node>& node) {
    if(!node)return {INT_MIN,INT_MIN};
    if(node->definition)return node->definition->priorities;
    if(node->weight>=1)return priorities(node->current);
    return maximum(priorities(node->previous),priorities(node->current));
}
AnimationPriorities ActionPlayback::basePriorities() const {
    auto moving=basePriorities_.at(base_.lowerChild());
    if(base_.upperChild()!=base_.lowerChild())moving=maximum(moving,basePriorities_.at(base_.upperChild()));
    // SpeedSkeletalAnimation priority getters select the control mode,
    // independently of the outgoing timed pose (original 0x83b080/0x83b0b0).
    return base_.speed()>0?moving:basePriorities_[0];
}
float ActionPlayback::alterNode(std::shared_ptr<Node>& node,float seconds) {
    if(!node)return 0;
    if(node->definition) {
        const double held=double(node->elapsed)+seconds;
        const float duration=node->definition->clip.durationFrames/node->definition->clip.framesPerSecond;
        node->elapsed=held>duration?duration:float(held);
        // CKAT stays alive even at its endpoint. Physical one-shot disposal
        // is separate; an inner TimedBlend discards a CKAT child's remainder.
        return held>duration?float(held-duration):0;
    }
    const double held=double(node->elapsed)+seconds;
    const bool finished=held>replacementDuration;
    node->elapsed=finished?replacementDuration:float(held);
    node->weight=finished?1:node->elapsed*(1/replacementDuration);
    float remaining=0;
    if(finished)node->previous.reset();
    if(node->weight<1) {
        (void)alterNode(node->previous,seconds);
        (void)alterNode(node->current,seconds);
    } else if(node->current) {
        // Original direct-current dispatch forwards its remainder but does
        // not install a returned nested replacement on this inner pointer.
        auto current=node->current;remaining=alterNode(current,seconds);
    }
    if(finished){auto current=node->current;node=std::move(current);}
    return remaining;
}
void ActionPlayback::advanceNode(std::shared_ptr<Node>& node,float seconds) {
    // Physical alter skips zero dt. Positive remainder is retried within this
    // same call; non-consuming one-shot alter clears the track. This captures
    // both the .005 pop branch and its smaller-remainder retry guard.
    while(node&&seconds>0) {
        const float remaining=alterNode(node,seconds);
        if(remaining<=0)return;
        if(remaining>=seconds){node.reset();return;}
        seconds=remaining;
    }
}
assets::Float3 ActionPlayback::advance(float seconds,float speed) {
    checkStep(seconds);
    if(!std::isfinite(speed)||speed<0)throw std::runtime_error("Invalid action movement speed");
    if(seconds==0)return {}; // Outer physical track skips the entire alter.
    const bool routedAtEntry=actionRouted_;
    const bool actionLocomotion=routedAtEntry&&priorities(action_).locomotion>=basePriorities().locomotion;
    assets::Float3 actionStep{};
    // The shipped idle actions have no locomotion. Preserve a leaf's authored
    // LOCT for explicit actions; nested replacement locomotion is unsupported.
    if(actionLocomotion&&action_&&action_->definition) {
        const auto& clip=action_->definition->clip;
        actionStep=clip.locomotionDelta(action_->elapsed*clip.framesPerSecond,seconds*clip.framesPerSecond);
    }
    advanceNode(action_,seconds);
    const auto baseStep=base_.advance(seconds,speed);
    // AGAT belongs to the idle SPAT child; its timer freezes with that child.
    if(ambient_&&speed==0)if(auto name=ambient_->advance(seconds,ambientEligible_))playAction(*name);
    if(actionRouted_&&!action_&&std::all_of(records_.begin(),records_.end(),[](const auto& r){return r.mode==2;})) {
        actionRouted_=false;actionName_.clear();
    }
    // A root mapping changed by AGAT during loop alter is only altered by
    // the outer combiner on the next controller call (original 0x82f943).
    if(routedAtEntry) {
        const auto a=priorities(action_),b=basePriorities();
        for(size_t i=0;i+1<records_.size();++i)records_[i].advance(seconds,a.transforms,b.transforms);
        records_.back().advance(seconds,a.locomotion,b.locomotion);
    }
    return actionLocomotion?actionStep:baseStep;
}
AnimationPose ActionPlayback::sampleNode(const std::shared_ptr<Node>& node) const {
    if(!node)return AnimationPose(skeleton_.bones.size()); // Null inner TimedBlend child is identity.
    if(node->definition) {
        const auto& definition=*node->definition;
        const float frame=std::min(definition.clip.durationFrames,node->elapsed*definition.clip.framesPerSecond);
        return sampleAnimationPose(skeleton_,&definition.clip,definition.bindings,frame);
    }
    if(node->weight>=1)return sampleNode(node->current);
    return blendAnimationTransforms(sampleNode(node->previous),sampleNode(node->current),node->weight);
}
AnimationPose ActionPlayback::sampleAnimation() {
    auto base=base_.sampleAnimation(skeleton_);if(!actionRouted_)return base;
    // Physical track cache updates only for transforms the priority combiner
    // evaluates. A missing physical animation retains its last evaluated pose.
    if(action_) {
        auto current=sampleNode(action_);
        for(size_t i=0;i<retained_.size();++i)if(records_[i].mode!=2)retained_[i]=current[i];
    }
    return evaluatePriorityBlend(retained_,base,records_);
}
std::vector<DirectX::XMMATRIX> ActionPlayback::sample(const assets::SkeletonData& skeleton) {
    if(skeleton.bones.size()!=skeleton_.bones.size())throw std::runtime_error("Action skeleton changed");
    return composeAnimationPose(skeleton_,sampleAnimation());
}
void ActionPlayback::reset() {
    base_.reset();action_.reset();actionRouted_=false;actionName_.clear();
    retained_.assign(skeleton_.bones.size(),{});records_.assign(skeleton_.bones.size()+1,{});
}
float ActionPlayback::actionFrame() const {
    auto node=action_;while(node&&!node->definition)node=node->current;
    return node?node->elapsed*node->definition->clip.framesPerSecond:0;
}
float ActionPlayback::replacementWeight() const {return action_&&!action_->definition?action_->weight:1;}
}
