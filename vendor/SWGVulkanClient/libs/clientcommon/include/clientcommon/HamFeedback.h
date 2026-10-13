#pragma once
#include "CreatureStatus.h"
#include <cmath>

namespace clientcommon {
struct HamFeedbackGeometry {
    HamBarGeometry bar;
    int recentCurrentX=0,recentCurrentWidth=0,recentMaximumX=0,recentMaximumWidth=0;
    bool recentCurrentVisible=false,recentMaximumVisible=false;
};
// SwgCuiMfdStatusBar::setValue 0xe166d0: geometry uses PRE-decay
// histories; then stores float32 histories with rates8/4 and epsilon1.
// State is per widget/row, never a smoothed replacement for server HAM.
class HamFeedback {
public:
    void reset(){known_=false;recentCurrent_=recentMaximum_=0;}
    float recentCurrent() const{return recentCurrent_;}
    float recentMaximum() const{return recentMaximum_;}
    HamFeedbackGeometry update(const std::optional<HamBarStatus>& value,int32_t scale,int width,float seconds) {
        HamFeedbackGeometry out;
        if(!value||value->maximum<=0||scale<=0||width<=0){reset();return out;}
        const auto& v=*value;out.bar=hamBarGeometry(v,scale,width);
        const auto usable=std::clamp<int64_t>(int64_t(v.maximum)-v.wounds,0,v.maximum);
        const float current=float(v.current),maximum=float(usable);
        // A newly known native row starts from its observed values. Missing
        // data and an identity change cannot create a cross-object trail.
        if(!known_){recentCurrent_=current;recentMaximum_=maximum;known_=true;}
        if(!std::isfinite(seconds)||seconds<0)seconds=0;
        auto pixels=[](double n){return int(std::clamp(n,0.,16384.));};
        out.recentMaximumX=out.bar.normal; // original currentMax END, not its start
        if(recentMaximum_>maximum) {
            out.recentMaximumVisible=true;
            out.recentMaximumWidth=pixels((double(recentMaximum_)-usable)*out.bar.normal/v.maximum);
            recentMaximum_=float(double(recentMaximum_)-(double(recentMaximum_)-usable)*seconds*4.);
            if(std::abs(double(recentMaximum_)-maximum)<=1.)recentMaximum_=maximum;
        }else recentMaximum_=maximum;
        recentCurrent_=std::min(recentCurrent_,maximum);
        out.recentCurrentX=out.bar.current;
        if(recentCurrent_>current&&usable) {
            out.recentCurrentVisible=true;
            out.recentCurrentWidth=pixels((double(recentCurrent_)-current)*out.bar.usable/usable);
            recentCurrent_=std::max(current,float(double(recentCurrent_)-(double(recentCurrent_)-v.current)*seconds*8.));
            if(std::abs(double(recentCurrent_)-current)<=1.)recentCurrent_=current;
        }else recentCurrent_=current;
        return out;
    }
private:
    bool known_=false;
    float recentCurrent_=0,recentMaximum_=0;
};
}
