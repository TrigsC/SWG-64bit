#include "TargetHud.h"
#include "assets/DdsImage.h"
#include "clientcommon/ChatText.h"
#include "clientcommon/ChatTabText.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace dummyclient {
TargetHud::TargetHud(renderer::VulkanRenderer& renderer,const HudAssetReader& reader,bool enableChat,bool enableNotices):gfx_(renderer),assets_(OriginalHudAssets::load(reader)) {
    if(enableChat||enableNotices)chat_=OriginalChatAssets::load(reader);
    if(enableNotices){noticeAssets_=OriginalScreenNoticeAssets::load(reader);notices_.emplace(noticeAssets_->fadeInSpeed,noticeAssets_->fadeOutSpeed);}
    std::set<std::string> paths;
    for(const auto& [_,s]:assets_.frameSlices)paths.insert(s.texturePath);
    for(const auto& [_,s]:assets_.postureFrameSlices)paths.insert(s.texturePath);
    for(const auto& [_,s]:assets_.selfFrameSlices)paths.insert(s.texturePath);
    for(const auto& [_,s]:assets_.selfPostureFrameSlices)paths.insert(s.texturePath);
    for(const auto& [_,g]:assets_.glyphs)paths.insert(g.texturePath);
    for(const auto& style:assets_.postureIcons)for(const auto& f:style.frames)paths.insert(f.image.texturePath);
    for(const auto& style:assets_.stateIcons)for(const auto& f:style.frames)paths.insert(f.image.texturePath);
    for(const auto* ham:{&assets_.targetHam,&assets_.selfHam})for(const auto& [_,row]:ham->rows)for(const auto* layer:{&row.normalMax,&row.current,&row.currentMax,&row.recentCurrent,&row.recentMax})
        for(const auto& [__,slice]:layer->style.slices)paths.insert(slice.texturePath);
    if(chat_){for(const auto& node:chat_->nodes)if(node.style)for(const auto& [_,slice]:node.style->slices)paths.insert(slice.texturePath);
        for(const auto& style:chat_->tabStyles)for(const auto& [_,slice]:style.slices)paths.insert(slice.texturePath);
        for(const auto& [_,font]:chat_->fonts)for(const auto& [__,glyph]:font.glyphs)paths.insert(glyph.texturePath);}
    if(paths.size()>16)throw std::runtime_error("HUD atlas count exceeds bounded budget");
    for(const auto& path:paths) {
        auto bytes=reader(path);if(!bytes)throw std::runtime_error("Missing HUD atlas: "+path);
        Texture texture;
        if(path.starts_with("texture/font/")) {
            auto image=decodeHudRgbaDds(*bytes);texture.width=image.width;texture.height=image.height;
            texture.handle=gfx_.loadTextureRGBA8(image.width,image.height,image.pixels);
        }else {
            auto image=assets::DdsImage::parse(*bytes);texture.width=image.width;texture.height=image.height;
            texture.handle=gfx_.loadTexture(image);
        }
        textures_.emplace(path,texture);
    }
    if(chat_)caretTexture_=gfx_.loadTextureRGBA8(1,1,{255,255,255,255});
    auto validate=[&](const HudImageSlice& s) {
        const auto& t=textures_.at(s.texturePath);const auto& r=s.source;
        if(std::min(r.x0,r.x1)<0||std::min(r.y0,r.y1)<0||std::max(r.x0,r.x1)>t.width||std::max(r.y0,r.y1)>t.height)
            throw std::runtime_error("HUD source rectangle exceeds atlas");
    };
    for(const auto& [_,s]:assets_.frameSlices)validate(s);
    for(const auto& [_,s]:assets_.postureFrameSlices)validate(s);
    for(const auto& [_,s]:assets_.selfFrameSlices)validate(s);
    for(const auto& [_,s]:assets_.selfPostureFrameSlices)validate(s);
    for(const auto& style:assets_.postureIcons)for(const auto& f:style.frames)validate(f.image);
    for(const auto& style:assets_.stateIcons)for(const auto& f:style.frames)validate(f.image);
    for(const auto* ham:{&assets_.targetHam,&assets_.selfHam})for(const auto& [_,row]:ham->rows)for(const auto* layer:{&row.normalMax,&row.current,&row.currentMax,&row.recentCurrent,&row.recentMax})
        for(const auto& [__,slice]:layer->style.slices)validate(slice);
    if(chat_){for(const auto& node:chat_->nodes)if(node.style)for(const auto& [_,slice]:node.style->slices)validate(slice);
        for(const auto& [_,font]:chat_->fonts)for(const auto& [__,glyph]:font.glyphs)validate({glyph.texturePath,glyph.source});}
    for(const auto& [_,g]:assets_.glyphs) {
        validate({g.texturePath,g.source});
        const auto& t=textures_.at(g.texturePath);
        if(g.source.x1<g.source.x0||g.source.y0+assets_.fontLeading>t.height)
            throw std::runtime_error("Invalid HUD bitmap glyph dimensions");
    }
}
void TargetHud::posture(const HudPostureWidget& widget,const clientcommon::CreatureStatus& status,float x,float y,int width,int height,float d,uint64_t tick) {
    if(!status.posture||*status.posture>=assets_.postureIcons.size())return;
    const auto& style=assets_.postureIcons[*status.posture];const auto selected=style.frameAt(tick);if(!selected)return;
    const auto& f=style.frames[*selected];const auto& l=widget.layout;
    // UIPacking centers with integer divisions, including odd parent sizes.
    x+=(width/2-l.width/2)*d;y+=(height/2-l.height/2)*d;
    clip_=std::array<float,4>{x,y,x+l.width*d,y+l.height*d};
    slice(f.image,x+(f.offsetX+f.proportionalX*l.width/100)*d,
        y+(f.offsetY+f.proportionalY*l.height/100)*d,l.width*d,l.height*d,widget.color);
    clip_.reset();
}
void TargetHud::prepare(const clientcommon::InspectionTarget* target,const clientcommon::InspectionTarget* player) {
    const uint64_t selfId=player?player->id:0,targetId=target?target->id:0;
    if(selfId!=selfFeedbackId_){for(auto& row:selfFeedback_)row.reset();selfFeedbackId_=selfId;}
    if(targetId!=targetFeedbackId_){for(auto& row:targetFeedback_)row.reset();targetFeedbackId_=targetId;}
    selfStates_.reconcile(player?&player->status:nullptr);
    targetStates_.reconcile(target?&target->status:nullptr);
    auto name=target?clientcommon::targetDisplayName(target->name):std::u16string{};
    if(name!=name_){name_=std::move(name);++replacements_;}
    auto ownName=player?clientcommon::targetDisplayName(player->name):std::u16string{};
    if(ownName!=playerName_){playerName_=std::move(ownName);++replacements_;}
}
void TargetHud::states(const HudStateVolume& v,const clientcommon::CreatureIndicators& controller,float x,float y,float delta,float d,uint64_t tick) {
    const float cx=x+v.composite.x*d+delta,cy=y+v.composite.y*d;
    x=cx+v.volume.x*d;y=cy+v.volume.y*d;
    const int columns=std::max(1,v.volume.width/v.cellWidth);size_t index=0;
    for(const auto& icon:controller.icons()) {
        const auto& style=assets_.stateIcons.at(icon.key);const auto selected=style.frameAt(tick);
        const float ix=x+int(index%columns)*v.cellWidth*d,iy=y+int(index/columns)*v.cellHeight*d;++index;
        if(!selected)continue;
        const auto& f=style.frames[*selected];auto tint=assets_.stateColors.at(icon.key).value_or(v.sampleColor);tint[3]*=icon.opacity;
        // Image, VolumePage and parent Composite each contribute clipping.
        clip_=std::array<float,4>{std::max(ix,x),std::max(iy,y),
            std::min({ix+v.cellWidth*d,x+v.volume.width*d,cx+v.composite.width*d}),
            std::min({iy+v.cellHeight*d,y+v.volume.height*d,cy+v.composite.height*d})};
        slice(f.image,ix+(f.offsetX+f.proportionalX*v.cellWidth/100)*d,
            iy+(f.offsetY+f.proportionalY*v.cellHeight/100)*d,v.cellWidth*d,v.cellHeight*d,tint);
        clip_.reset();
    }
}
void TargetHud::slice(const HudImageSlice& s,float x,float y,float w,float h,const std::array<float,4>& color) {
    if(w<=0||h<=0)return;
    const auto& t=textures_.at(s.texturePath);const auto& r=s.source;
    float left=x,top=y,right=x+w,bottom=y+h;
    if(clip_) {left=std::max(left,(*clip_)[0]);top=std::max(top,(*clip_)[1]);right=std::min(right,(*clip_)[2]);bottom=std::min(bottom,(*clip_)[3]);}
    if(right<=left||bottom<=top)return;
    auto mix=[](float a,float b,float f){return a+(b-a)*f;};
    gfx_.drawLabel({(left+right)/2,(top+bottom)/2,0},right-left,bottom-top,{1,0,0},{0,-1,0},t.handle,
        {mix(r.x0,r.x1,(left-x)/w)/t.width,mix(r.y0,r.y1,(top-y)/h)/t.height,
         mix(r.x0,r.x1,(right-x)/w)/t.width,mix(r.y0,r.y1,(bottom-y)/h)/t.height},
        {color[0],color[1],color[2],color[3]});
}
void TargetHud::frame(const std::map<std::string,HudImageSlice>& s,float x,float y,float w,float h,float d,const std::array<float,4>& color) {
    auto width=[&](const char* k){auto it=s.find(k);if(it==s.end())return 0.f;const auto& r=it->second.source;return float(std::abs(r.x1-r.x0))*d;};
    auto height=[&](const char* k){auto it=s.find(k);if(it==s.end())return 0.f;const auto& r=it->second.source;return float(std::abs(r.y1-r.y0))*d;};
    // Original UIRectangleStyle uses edge dimensions for corner destinations.
    float l=std::min(width("w"),w/2),r=std::min(width("e"),w/2);
    float t=std::min(height("n"),h/2),b=std::min(height("s"),h/2);
    auto draw=[&](const char* k,float xx,float yy,float ww,float hh){if(auto it=s.find(k);it!=s.end())slice(it->second,xx,yy,ww,hh,color);};
    draw("c",x+l,y+t,w-l-r,h-t-b);
    draw("nw",x,y,l,t);draw("ne",x+w-r,y,r,t);draw("sw",x,y+h-b,l,b);draw("se",x+w-r,y+h-b,r,b);
    draw("n",x+l,y,w-l-r,t);draw("s",x+l,y+h-b,w-l-r,b);
    draw("w",x,y+t,l,h-t-b);draw("e",x+w-r,y+t,r,h-t-b);
}
void TargetHud::ham(const HudHamLayout& layout,const clientcommon::CreatureStatus& status,std::array<clientcommon::HamFeedback,4>& feedback,float x,float y,float delta,float d,float seconds) {
    auto reset=[&](){for(auto& row:feedback)row.reset();};
    if(layout.hasAuthoredForceRow&&!status.forceKnown){reset();return;}
    auto scale=status.sharedMaximum();if(!scale){reset();return;}
    x+=(layout.composite.x+layout.ham.x)*d+delta;
    y+=(layout.composite.y+layout.ham.y)*d;
    size_t index=0;
    const bool force=layout.hasAuthoredForceRow&&status.force.has_value();
    const int height=layout.ham.height/(force?4:3);
    for(const auto* key:{"h","a","m","f"}) {
        if(std::string(key)=="f"&&!force){feedback[3].reset();break;}
        const auto& row=layout.rows.at(key);const auto& value=index<3?status.ham[index]:status.force;
        const auto geometry=feedback[index].update(value,index<3?*scale:(value?value->maximum:0),row.layout.width,seconds);
        const float rowY=float(index++*height);if(!value)continue;
        const auto& bar=geometry.bar;
        const float rx=x+(row.layout.x+row.inner.x)*d,ry=y+(rowY+row.inner.y)*d;
        clip_=std::array<float,4>{rx,ry,rx+bar.normal*d,ry+height*d};
        auto paint=[&](const HudBarLayer& layer,float xx,float width) {
            std::array<float,4> color;
            for(size_t c=0;c<4;++c)color[c]=row.innerAppearance.color[c]*layer.appearance.color[c]*layer.appearance.backgroundTint[c]*layer.style.color[c];
            color[3]*=layer.appearance.backgroundOpacity*layer.style.opacity;
            const int layerHeight=std::clamp(height,layer.layout.minimumHeight,layer.layout.maximumHeight);
            frame(layer.style.slices,xx,ry+layer.layout.y*d,width,layerHeight*d,d,color);
        };
        paint(row.normalMax,rx+row.normalMax.layout.x*d,bar.normal*d);
        // UIPage renders its authored child list in reverse. Recent layers
        // lie behind current/currentMax; widths are pre-decay observations.
        if(geometry.recentCurrentVisible)paint(row.recentCurrent,rx+geometry.recentCurrentX*d,geometry.recentCurrentWidth*d);
        if(row.hasRecentMaximum&&geometry.recentMaximumVisible)
            paint(row.recentMax,rx+geometry.recentMaximumX*d,geometry.recentMaximumWidth*d);
        paint(row.currentMax,rx+bar.usable*d,(bar.normal-bar.usable)*d);
        paint(row.current,rx,bar.current*d);
        clip_.reset();
    }
}
void TargetHud::text(const std::u16string& string,float x,float y,float w,float h,float d,bool right,const std::array<float,4>& color) {
    if(w<=0||h<=0)return;
    std::vector<const HudGlyph*> glyphs;
    float advance=0;
    for(size_t i=0;i<string.size();++i) {
        uint32_t code=string[i];
        if(code>=0xd800&&code<=0xdbff&&i+1<string.size()&&string[i+1]>=0xdc00&&string[i+1]<=0xdfff)
            code=0x10000+((code-0xd800)<<10)+(string[++i]-0xdc00);
        auto found=assets_.glyphs.find(code);if(found==assets_.glyphs.end())found=assets_.glyphs.find(0);
        glyphs.push_back(&found->second);advance+=found->second.advance*d;
    }
    float cursor=right?x+w-advance:x;
    for(const auto* g:glyphs) {
        const auto& texture=textures_.at(g->texturePath);const auto& r=g->source;
        float gx=cursor+g->advancePre*d,gy=y,gw=std::abs(r.x1-r.x0)*d,gh=assets_.fontLeading*d;
        float left=std::max(gx,x),top=std::max(gy,y),rr=std::min(gx+gw,x+w),bottom=std::min(gy+gh,y+h);
        if(gw>0&&gh>0&&rr>left&&bottom>top) {
            auto mix=[](float a,float b,float fraction){return a+(b-a)*fraction;};
            DirectX::XMFLOAT4 uv{
                mix(float(r.x0),float(r.x1),(left-gx)/gw)/texture.width,
                mix(float(r.y0),float(r.y0+assets_.fontLeading),(top-gy)/gh)/texture.height,
                mix(float(r.x0),float(r.x1),(rr-gx)/gw)/texture.width,
                mix(float(r.y0),float(r.y0+assets_.fontLeading),(bottom-gy)/gh)/texture.height};
            gfx_.drawLabel({(left+rr)/2,(top+bottom)/2,0},rr-left,bottom-top,{1,0,0},{0,-1,0},texture.handle,uv,
                {color[0],color[1],color[2],color[3]});
        }
        cursor+=g->advance*d;
    }
}
void TargetHud::draw(const clientcommon::InspectionTarget* target,clientcommon::TargetPoint,float d,const clientcommon::InspectionTarget* player,std::optional<uint64_t> animationTick,std::optional<float> feedbackSeconds) {
    if(!target&&!player)return;
    const auto now=std::chrono::steady_clock::now();
    const float seconds=feedbackSeconds.value_or(lastFeedbackDraw_?float(std::chrono::duration<double>(now-*lastFeedbackDraw_).count()):0.f);
    lastFeedbackDraw_=now;
    gfx_.beginOverlayPass();
    if(!std::isfinite(d)||d<=0)d=1;
    const float screen=float(gfx_.renderWidth());if(screen<122*d)return;
    const uint64_t tick=animationTick.value_or(uint64_t(std::chrono::duration<double>(std::chrono::steady_clock::now()-uiEpoch_).count()*60));
    if(!animationTick&&(!lastHeartbeatTick_||tick!=*lastHeartbeatTick_)){stateHeartbeat();lastHeartbeatTick_=tick;}
    if(player) {
        const float pw=std::clamp(screen*assets_.selfPackWidthRatio,122*d,screen);
        const float px=assets_.selfOffsetX*d,py=assets_.selfOffsetY*d,pdelta=pw-assets_.selfWidth*d;
        frame(assets_.selfPostureFrameSlices,px+assets_.selfPostureFrameX*d,py+assets_.selfPostureFrameY*d,
            assets_.selfPostureFrameWidth*d,assets_.selfPostureFrameHeight*d,d,assets_.selfPostureFrameColor);
        frame(assets_.selfFrameSlices,px+assets_.selfFrameX*d,py+assets_.selfFrameY*d,
            assets_.selfFrameWidth*d+pdelta,assets_.selfFrameHeight*d,d,assets_.selfFrameColor);
        ham(assets_.selfHam,player->status,selfFeedback_,px,py,0,d,seconds);
        states(assets_.selfStates,selfStates_,px,py,0,d,tick);
        posture(assets_.selfPostureIcon,player->status,px+assets_.selfPostureFrameX*d,py+assets_.selfPostureFrameY*d,
            assets_.selfPostureFrameWidth,assets_.selfPostureFrameHeight,d,tick);
        // UIText::GetTextPos centers with two integer divisions.
        const float nameY=py+assets_.selfNameY*d+(assets_.selfNameHeight/2-assets_.fontLeading/2)*d;
        text(playerName_,px+assets_.selfNameX*d,nameY,assets_.selfNameWidth*d+pdelta,assets_.selfNameHeight*d,d,false,assets_.selfNameColor);
    }
    if(!target)return;
    float w=std::clamp(screen*assets_.targetPackWidthRatio,122*d,screen),x=screen-w,y=assets_.targetOffsetY*d;
    float delta=w-assets_.targetWidth*d;
    frame(assets_.postureFrameSlices,x+assets_.targetPostureFrameX*d+delta,y+assets_.targetPostureFrameY*d,
        assets_.targetPostureFrameWidth*d,assets_.targetPostureFrameHeight*d,d,assets_.frameColor);
    frame(assets_.frameSlices,x+assets_.targetFrameX*d,y+assets_.targetFrameY*d,
        assets_.targetFrameWidth*d+delta,assets_.targetFrameHeight*d,d,assets_.frameColor);
    ham(assets_.targetHam,target->status,targetFeedback_,x,y,delta,d,seconds);
    states(assets_.targetStates,targetStates_,x,y,delta,d,tick);
    posture(assets_.targetPostureIcon,target->status,x+assets_.targetPostureFrameX*d+delta,y+assets_.targetPostureFrameY*d,
        assets_.targetPostureFrameWidth,assets_.targetPostureFrameHeight,d,tick);
    text(assets_.targetLabel,x+assets_.targetLabelX*d,y+assets_.targetLabelY*d,assets_.targetLabelWidth*d,assets_.targetLabelHeight*d,d,false,assets_.labelColor);
    text(name_,x+assets_.targetNameX*d,y+assets_.targetNameY*d,assets_.targetNameWidth*d+delta,assets_.targetNameHeight*d,d,true,assets_.nameColor);
}
std::array<int,4> TargetHud::drawChat(const clientcommon::ChatSnapshot& snapshot,const renderer::TextFocus& input,float d,int wheelDelta,std::optional<size_t> activeTab,std::optional<size_t> hoveredTab) {
    if(!chat_)return {};if(!std::isfinite(d)||d<=0)d=1;gfx_.beginOverlayPass();
    const int screenW=int(gfx_.renderWidth()/d),screenH=int(gfx_.renderHeight()/d);const auto& source=*chat_;
    struct Position {int x=0,y=0,w=0,h=0;std::array<float,4> clip{},color{1,1,1,1};bool visible=true;};
    std::vector<Position> positions(source.nodes.size());
    auto split=[](std::string text){std::array<std::string,2> out;auto comma=text.find(',');out[0]=text.substr(0,comma);out[1]=comma==std::string::npos?out[0]:text.substr(comma+1);return out;};
    auto packSize=[](const std::string& rule,int oldSize,int before,int now){if(rule=="a")return oldSize+now-before;if(rule=="p")return before?int(double(oldSize)*now/before):oldSize;if(rule=="1")return now;return oldSize;};
    auto packLocation=[](const std::string& rule,int old,int oldSize,int newSize,int before,int now){if(rule=="fff")return old+now-before-newSize+oldSize;if(rule=="cfc"||rule=="cpc")return old+now/2-before/2-newSize/2+oldSize/2;return old;};
    for(size_t i=0;i<source.nodes.size();++i) {
        const auto& n=source.nodes[i];const auto& l=n.layout;auto& p=positions[i];auto size=split(l.packSize),location=split(l.packLocation);
        const int beforeW=n.parent<0?1024:source.nodes[n.parent].layout.width,beforeH=n.parent<0?768:source.nodes[n.parent].layout.height;
        const int nowW=n.parent<0?screenW:positions[n.parent].w,nowH=n.parent<0?screenH:positions[n.parent].h;
        p.w=std::clamp(packSize(size[0],l.width,beforeW,nowW),l.minimumWidth,l.maximumWidth);
        p.h=std::clamp(packSize(size[1],l.height,beforeH,nowH),l.minimumHeight,l.maximumHeight);
        p.x=packLocation(location[0],l.x,l.width,p.w,beforeW,nowW);p.y=packLocation(location[1],l.y,l.height,p.h,beforeH,nowH);
        std::array<float,4> parentClip{0,0,float(gfx_.renderWidth()),float(gfx_.renderHeight())};
        p.color=n.appearance.color;p.visible=n.visible;
        if(n.parent>=0){const auto& parent=positions[n.parent];p.x+=parent.x;p.y+=parent.y;parentClip=parent.clip;p.visible=p.visible&&parent.visible;for(size_t c=0;c<4;++c)p.color[c]*=parent.color[c];}
        p.clip={std::max(p.x*d,parentClip[0]),std::max(p.y*d,parentClip[1]),std::min((p.x+p.w)*d,parentClip[2]),std::min((p.y+p.h)*d,parentClip[3])};
    }
    // Parent backgrounds precede their reverse-ordered child list. Keeping
    // both authored output backgrounds is deliberate, despite shared names.
    std::function<void(size_t)> paintNode=[&](size_t i) {
        const auto& n=source.nodes[i];const auto& p=positions[i];if(!p.visible)return;
        if(n.style){auto tint=p.color;for(size_t c=0;c<4;++c)tint[c]*=n.appearance.backgroundTint[c]*n.style->color[c];tint[3]*=n.appearance.backgroundOpacity*n.style->opacity;
            clip_=p.clip;frame(n.style->slices,p.x*d,p.y*d,p.w*d,p.h*d,d,tint);clip_.reset();}
        for(size_t j=source.nodes.size();j-->i+1;)if(source.nodes[j].parent==int(i))paintNode(j);
    };
    paintNode(0);
    const auto& root=positions[0];chatAreaRect_={int(root.x*d),int(root.y*d),int((root.x+root.w)*d),int((root.y+root.h)*d)};
    auto glyph=[&](const HudBitmapFont& font,uint32_t code)->const HudGlyph& {auto found=font.glyphs.find(code);return found==font.glyphs.end()?font.glyphs.at(0):found->second;};
    auto drawLine=[&](const HudBitmapFont& font,const clientcommon::ChatTextLine& line,float x,float y) {
        for(const auto& c:line){const auto& g=glyph(font,c.code);const auto& t=textures_.at(g.texturePath);const auto& r=g.source;
            HudImageSlice image{g.texturePath,{r.x0,r.y0,r.x1,r.y0+font.leading}};
            slice(image,x+g.advancePre*d,y,std::abs(r.x1-r.x0)*d,font.leading*d,c.color);x+=g.advance*d;}
    };
    if(activeTab!=chatActiveTab_){chatActiveTab_=activeTab;chatScrollLines_=chatWheelRemainder_=0;chatRevision_=UINT64_MAX;}
    chatTabRects_.clear();
    if(activeTab){const auto& pane=positions[source.tabs];chatAreaRect_[1]=std::min(chatAreaRect_[1],int(pane.clip[1]));const auto& tabFont=source.fonts.at("bold_12");int x=pane.x;
        for(size_t i=0;i<source.tabLabels.size();++i){int width=source.tabTextMargin[0]+source.tabTextMargin[2];for(auto c:source.tabLabels[i])width+=glyph(tabFont,c).advance;
            const auto state=i==*activeTab?1u:hoveredTab&&i==*hoveredTab?2u:0u;const auto& style=source.tabStyles[state];
            clip_=pane.clip;auto tint=style.color;for(size_t c=0;c<4;++c)tint[c]*=pane.color[c];tint[3]*=style.opacity;
            frame(style.slices,x*d,pane.y*d,width*d,pane.h*d,d,tint);
            clientcommon::ChatTextLine label;for(auto c:source.tabLabels[i])label.push_back({uint32_t(c),source.tabTextColors[state]});
            drawLine(tabFont,label,(x+source.tabTextMargin[0])*d,(pane.y+pane.h/2-tabFont.leading/2)*d);
            chatTabRects_.push_back({int(std::max(x*d,pane.clip[0])),int(pane.clip[1]),int(std::min((x+width)*d,pane.clip[2])),int(pane.clip[3])});x+=width;
        }clip_.reset();}
    const auto& output=positions[source.output];const auto& font=source.fonts.at("default_12");
    const int wrapWidth=std::max(1,output.w-source.outputMargin[0]-source.outputMargin[2]);
    if(chatRevision_!=snapshot.revision||chatWrapWidth_!=wrapWidth) {
        const auto oldCount=chatLines_.size();chatLines_.clear();
        if(activeTab){std::u16string history;for(const auto& message:snapshot.lines)clientcommon::appendTabText(history,message.text);
            chatLines_=clientcommon::wrapChatText(history,wrapWidth,[&](uint32_t c){return glyph(font,c).advance;},source.outputColor,source.palette);}
        else for(const auto& message:snapshot.lines){auto lines=clientcommon::wrapChatText(message.text,wrapWidth,[&](uint32_t c){return glyph(font,c).advance;},source.outputColor,source.palette);for(auto& line:lines)chatLines_.push_back(std::move(line));}
        if(chatLines_.size()>4096)chatLines_.erase(chatLines_.begin(),chatLines_.end()-4096);
        if(chatScrollLines_>0&&chatWrapWidth_==wrapWidth&&chatLines_.size()>oldCount)chatScrollLines_+=int(chatLines_.size()-oldCount);
        chatRevision_=snapshot.revision;chatWrapWidth_=wrapWidth;
    }
    clip_=output.clip;(*clip_)[0]=std::max((*clip_)[0],(output.x+source.outputMargin[0])*d);(*clip_)[1]=std::max((*clip_)[1],(output.y+source.outputMargin[1])*d);
    (*clip_)[2]=std::min((*clip_)[2],(output.x+output.w-source.outputMargin[2])*d);(*clip_)[3]=std::min((*clip_)[3],(output.y+output.h-source.outputMargin[3])*d);
    const size_t visible=std::max(1,(output.h-source.outputMargin[1]-source.outputMargin[3])/font.leading);
    chatWheelRemainder_+=wheelDelta*3;const int scrollDelta=chatWheelRemainder_/120;chatWheelRemainder_%=120;
    chatScrollLines_=std::clamp(chatScrollLines_+scrollDelta,0,int(chatLines_.size()>visible?chatLines_.size()-visible:0));
    const size_t end=chatLines_.size()-chatScrollLines_,first=end>visible?end-visible:0;
    for(size_t i=first;i<end;++i)drawLine(font,chatLines_[i],(output.x+source.outputMargin[0])*d,(output.y+source.outputMargin[1]+int(i-first)*font.leading)*d);
    clip_.reset();
    const auto& box=positions[source.input];const auto& inputFont=source.fonts.at("default_13");
    const int ix=box.x+source.inputPadding[0],iy=box.y+source.inputPadding[1],iw=std::max(1,box.w-source.inputPadding[0]-source.inputPadding[2]);
    clientcommon::ChatTextLine draft;int cursorWidth=0,fullWidth=0;
    for(size_t i=0;i<input.draft().size();) {size_t before=i;uint32_t code=input.draft()[i++];if(code>=0xd800&&code<=0xdbff&&i<input.draft().size())code=0x10000+((code-0xd800)<<10)+(input.draft()[i++]-0xdc00);
        const int advance=glyph(inputFont,code).advance;draft.push_back({code,source.inputColor});fullWidth+=advance;if(before<input.cursor())cursorWidth+=advance;}
    const int offset=std::max(0,cursorWidth-iw+source.caretWidth);
    clip_=box.clip;(*clip_)[0]=std::max((*clip_)[0],ix*d);(*clip_)[1]=std::max((*clip_)[1],iy*d);
    (*clip_)[2]=std::min((*clip_)[2],(ix+iw)*d);(*clip_)[3]=std::min((*clip_)[3],(box.y+box.h-source.inputPadding[3])*d);
    auto solid=[&](float x,float y,float w,float h,std::array<float,4> color){if(clip_){const auto& c=*clip_;float right=std::min(x+w,c[2]),bottom=std::min(y+h,c[3]);x=std::max(x,c[0]);y=std::max(y,c[1]);w=right-x;h=bottom-y;}if(w>0&&h>0)gfx_.drawLabel({x+w/2,y+h/2,0},w,h,{1,0,0},{0,-1,0},caretTexture_,{0,0,1,1},{color[0],color[1],color[2],color[3]});};
    if(input.active()&&input.selected())solid((ix-offset)*d,iy*d,fullWidth*d,inputFont.leading*d,{85.f/255,85.f/255,85.f/255,1});
    drawLine(inputFont,draft,(ix-offset)*d,iy*d);
    if(input.active())solid((ix+cursorWidth-offset)*d,iy*d,source.caretWidth*d,inputFont.leading*d,source.caretColor);
    clip_.reset();
    // This milestone has a local draft field. The capability label prevents
    // an Enter key from pretending that an unsubmitted message was sent.
    if(input.active()||activeTab) {const auto& channel=positions[source.channel];clientcommon::ChatTextLine label;
        const auto caption=activeTab&&*activeTab<source.tabLabels.size()?source.tabLabels[*activeTab]:input.submissionEnabled()?std::u16string(u"Say"):std::u16string(u"Draft");
        for(char16_t c:caption)label.push_back({uint32_t(c),source.channelColor});const auto& f=source.fonts.at("bold_12");int width=0;for(const auto& c:label)width+=glyph(f,c.code).advance;
        clip_=channel.clip;drawLine(f,label,(channel.x+channel.w/2-width/2)*d,(channel.y+channel.h/2-f.leading/2)*d);clip_.reset();}
    return {int(box.x*d),int(box.y*d),int((box.x+box.w)*d),int((box.y+box.h)*d)};
}

void TargetHud::drawNotices(const clientcommon::ChatSnapshot& raw,const clientcommon::ChatSnapshot& presented,float d,std::optional<uint64_t> suppliedTick,bool pulse){
    if(!notices_||!noticeAssets_||!chat_)return;if(!std::isfinite(d)||d<=0)d=1;
    const auto tick=suppliedTick.value_or(uint64_t(std::chrono::duration<double>(std::chrono::steady_clock::now()-uiEpoch_).count()*60));
    const auto& a=*noticeAssets_;const auto& font=chat_->fonts.at("bold_13");
    const int screenW=int(gfx_.renderWidth()/d),screenH=int(gfx_.renderHeight()/d);
    const int pageW=std::max(1,int(double(a.page.width)*screenW/1024)),pageH=std::max(1,int(double(a.page.height)*screenH/768));
    const int pageX=a.page.x+screenW/2-1024/2-pageW/2+a.page.width/2,pageY=a.page.y;
    if(!noticeCompositeSize_[0])noticeCompositeSize_={a.composite.width,a.composite.height};
    std::array<int,2> desired{std::max(1,a.composite.width+pageW-a.page.width),std::max(1,a.composite.height+pageH-a.page.height)};
    for(size_t axis=0;axis<2;++axis)desired[axis]-=(desired[axis]-noticeCompositeSize_[axis])%a.sizeIncrement[axis];
    noticeCompositeSize_=desired;const int w=desired[0],h=desired[1];
    auto glyph=[&](uint32_t code)->const HudGlyph&{auto found=font.glyphs.find(code);return found==font.glyphs.end()?font.glyphs.at(0):found->second;};
    notices_->synchronize(raw,presented,tick);notices_->update(tick);
    if(pulse&&(!lastNoticeTick_||tick!=*lastNoticeTick_)){noticeHeartbeat();lastNoticeTick_=tick;}
    struct Layout{std::vector<clientcommon::ChatTextLine> lines;std::vector<int> widths;};std::map<uint64_t,Layout> layout;
    for(auto& row:notices_->rows()){
        std::vector<int> measuredWidths;clientcommon::wrapChatText(row.text,16384-a.margin[0]-a.margin[2],[&](uint32_t c){return glyph(c).advance;},a.textColor,chat_->palette,&measuredWidths);int natural=0;
        for(int width:measuredWidths)natural=std::max(natural,width);
        if(!row.parentWidth)row.width=std::min(w,natural+1+a.margin[0]+a.margin[2]);else row.width=std::clamp(row.width+w-row.parentWidth,1,16384);row.parentWidth=w;
        std::vector<int> widths;auto lines=clientcommon::wrapChatText(row.text,std::max(1,row.width-a.margin[0]-a.margin[2]),[&](uint32_t c){return glyph(c).advance;},a.textColor,chat_->palette,&widths);
        row.height=int(lines.size())*font.leading+a.margin[1]+a.margin[3];layout.emplace(row.sequence,Layout{std::move(lines),std::move(widths)});
    }
    notices_->packAndCull(w,h,a.spacing,notices_->admissions()!=lastNoticeAdmissions_);lastNoticeAdmissions_=notices_->admissions();gfx_.beginOverlayPass();clip_=std::array<float,4>{pageX*d,pageY*d,(pageX+std::min(pageW,w))*d,(pageY+std::min(pageH,h))*d};
    const auto panelClip=*clip_;
    for(const auto& row:notices_->rows()){
        const auto& lines=layout.at(row.sequence).lines;size_t limit=clientcommon::ScreenNotices::unrollLimit(row,tick,a.unrollSpeed),emitted=0;const float alpha=clientcommon::ScreenNotices::textAlpha(row,a.textMinimum,noticeParentOpacity_);
        const int innerW=row.width-a.margin[0]-a.margin[2],innerH=row.height-a.margin[1]-a.margin[3];
        clip_=std::array<float,4>{std::max(panelClip[0],(pageX+row.x+a.margin[0])*d),std::max(panelClip[1],(pageY+row.y+a.margin[1])*d),std::min(panelClip[2],(pageX+row.x+row.width-a.margin[2])*d),std::min(panelClip[3],(pageY+row.y+row.height-a.margin[3])*d)};
        int y=pageY+row.y+a.margin[1]+innerH/2-int(lines.size())*font.leading/2;
        size_t lineIndex=0;for(const auto& line:lines){const int width=layout.at(row.sequence).widths[lineIndex++];const float startX=(pageX+row.x+a.margin[0]+std::max(0,(innerW-width)/2))*d;
            // Original ground-HUD workspace enables bright-text auto shadow;
            // UITextStyle renders the black (1,1) offset before each full line.
            const bool shadow=a.textColor[0]+a.textColor[1]+a.textColor[2]>=200.f/255.f;
            for(int pass=shadow?0:1;pass<2;++pass){float x=startX;size_t count=emitted;
                for(const auto& c:line){if(limit&&count>=limit)break;++count;const auto& g=glyph(c.code);auto tint=pass?c.color:std::array<float,4>{0,0,0,c.color[3]};tint[3]*=alpha;
                    slice({g.texturePath,{g.source.x0,g.source.y0,g.source.x1,g.source.y0+font.leading}},x+(g.advancePre+(pass?0:1))*d,(y+(pass?0:1))*d,std::abs(g.source.x1-g.source.x0)*d,font.leading*d,tint);x+=g.advance*d;}
                if(pass)emitted=count;
            }
            y+=font.leading;
        }
    }
    clip_.reset();
}

}
