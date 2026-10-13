#pragma once
#include "OriginalHudAssets.h"
#include "clientcommon/TargetSelection.h"
#include "clientcommon/CreatureIndicators.h"
#include "clientcommon/HamFeedback.h"
#include "clientcommon/ChatLog.h"
#include "clientcommon/ChatText.h"
#include "clientcommon/ScreenNotices.h"
#include "renderer/TextFocus.h"
#include "renderer/VulkanRenderer.h"
#include <chrono>

namespace dummyclient {
// Render the bounded target-widget subset from the installed SWG resources.
class TargetHud {
public:
    TargetHud(renderer::VulkanRenderer& renderer,const HudAssetReader& reader,bool enableChat=false,bool enableNotices=false);
    void prepare(const clientcommon::InspectionTarget* target,const clientcommon::InspectionTarget* player=nullptr);
    void draw(const clientcommon::InspectionTarget* target,clientcommon::TargetPoint self,float density=1.f,const clientcommon::InspectionTarget* player=nullptr,std::optional<uint64_t> animationTick={},std::optional<float> feedbackSeconds={});
    size_t nameReplacements() const { return replacements_; }
    size_t textureCount() const { return textures_.size(); }
    void stateHeartbeat(){selfStates_.heartbeat(assets_.stateFadeSpeed);targetStates_.heartbeat(assets_.stateFadeSpeed);}
    void resetHamFeedback(){for(auto* p:{&selfFeedback_,&targetFeedback_})for(auto& row:*p)row.reset();}
    std::array<int,4> drawChat(const clientcommon::ChatSnapshot&,const renderer::TextFocus&,float density=1.f,int wheelDelta=0,std::optional<size_t> activeTab={},std::optional<size_t> hoveredTab={});
    const std::vector<std::array<int,4>>& chatTabRects()const{return chatTabRects_;}
    void drawNotices(const clientcommon::ChatSnapshot& raw,const clientcommon::ChatSnapshot& presented,float density=1.f,std::optional<uint64_t> tick={},bool pulse=true);
    void noticeHeartbeat(){if(notices_)notices_->heartbeat();if(noticeAssets_)noticeParentOpacity_=std::max(noticeAssets_->parentIdleOpacity,noticeParentOpacity_-noticeAssets_->parentFadeSpeed/60.f);}
    void resetNotices(){if(noticeAssets_){notices_.emplace(noticeAssets_->fadeInSpeed,noticeAssets_->fadeOutSpeed);noticeParentOpacity_=noticeAssets_->parentIdleOpacity;}lastNoticeTick_.reset();lastNoticeAdmissions_=0;noticeCompositeSize_={0,0};}
    const clientcommon::ScreenNotices* noticeState()const{return notices_?&*notices_:nullptr;}
    std::array<int,4> chatAreaRect() const{return chatAreaRect_;}
private:
    struct Texture { renderer::TextureHandle handle; int width=0,height=0; };
    void slice(const HudImageSlice&,float x,float y,float w,float h,const std::array<float,4>& color);
    void frame(const std::map<std::string,HudImageSlice>&,float x,float y,float w,float h,float density,const std::array<float,4>& color);
    void ham(const HudHamLayout&,const clientcommon::CreatureStatus&,std::array<clientcommon::HamFeedback,4>&,float x,float y,float delta,float density,float seconds);
    void posture(const HudPostureWidget&,const clientcommon::CreatureStatus&,float x,float y,int width,int height,float density,uint64_t tick);
    void states(const HudStateVolume&,const clientcommon::CreatureIndicators&,float x,float y,float delta,float density,uint64_t tick);
    void text(const std::u16string&,float x,float y,float w,float h,float density,bool right,const std::array<float,4>& color);
    renderer::VulkanRenderer& gfx_;
    OriginalHudAssets assets_;
    std::optional<OriginalChatAssets> chat_;
    std::optional<OriginalScreenNoticeAssets> noticeAssets_;
    std::optional<clientcommon::ScreenNotices> notices_;
    std::optional<uint64_t> lastNoticeTick_;uint64_t lastNoticeAdmissions_=0;
    std::array<int,2> noticeCompositeSize_{};
    float noticeParentOpacity_=1;
    renderer::TextureHandle caretTexture_{};
    uint64_t chatRevision_=UINT64_MAX;
    int chatWrapWidth_=-1;
    std::vector<clientcommon::ChatTextLine> chatLines_;
    int chatScrollLines_=0,chatWheelRemainder_=0;
    std::array<int,4> chatAreaRect_{};
    std::vector<std::array<int,4>> chatTabRects_;
    std::optional<size_t> chatActiveTab_;
    std::map<std::string,Texture> textures_;
    std::u16string name_;
    std::u16string playerName_;
    size_t replacements_=0;
    std::optional<std::array<float,4>> clip_;
    clientcommon::CreatureIndicators selfStates_,targetStates_;
    std::optional<uint64_t> lastHeartbeatTick_;
    std::array<clientcommon::HamFeedback,4> selfFeedback_,targetFeedback_;
    uint64_t selfFeedbackId_=0,targetFeedbackId_=0;
    std::optional<std::chrono::steady_clock::time_point> lastFeedbackDraw_;
    const std::chrono::steady_clock::time_point uiEpoch_=std::chrono::steady_clock::now();
};
}
