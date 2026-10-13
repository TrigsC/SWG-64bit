#include "clientcommon/TargetSelection.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <vector>

namespace clientcommon {
namespace {
bool finite(TargetPoint p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
}
std::optional<float> TargetSelection::distance(const InspectionTarget& t,TargetPoint self) {
    if(!t.resolved||!finite(t.position)||!finite(self))return {};
    double dx=double(t.position.x)-self.x,dy=double(t.position.y)-self.y,dz=double(t.position.z)-self.z;
    double d=std::sqrt(dx*dx+dy*dy+dz*dz);
    if(d>std::numeric_limits<float>::max())return {};
    return float(d);
}
const InspectionTarget* TargetSelection::selected(std::span<const InspectionTarget> targets) const {
    if(!id_)return nullptr;
    auto it=std::find_if(targets.begin(),targets.end(),[&](const auto& t){return t.id==id_&&!t.self;});
    return it==targets.end()?nullptr:&*it;
}
void TargetSelection::reconcile(std::span<const InspectionTarget> targets) {
    if(!selected(targets))clear(); // absent/destroyed/self; unresolved positions retain the ID
}
void TargetSelection::cycle(std::span<const InspectionTarget> targets,TargetPoint self) {
    std::vector<std::pair<float,uint64_t>> ordered;
    for(const auto& t:targets)if(t.id&&!t.self) {
        auto d=distance(t,self);if(d&&*d<=rangeMeters)ordered.emplace_back(*d,t.id);
    }
    std::sort(ordered.begin(),ordered.end());
    if(ordered.empty()){clear();return;}
    auto it=std::find_if(ordered.begin(),ordered.end(),[&](auto p){return p.second==id_;});
    id_=it==ordered.end()||++it==ordered.end()?ordered.front().second:it->second;
}
void TargetSelection::pick(std::span<const InspectionTarget> targets,TargetPoint self,
                           TargetPoint origin,TargetPoint direction) {
    if(!finite(origin)||!finite(direction)){clear();return;}
    double length=std::hypot(std::hypot(double(direction.x),direction.y),direction.z);
    if(length<1e-9){clear();return;}
    double nx=direction.x/length,ny=direction.y/length,nz=direction.z/length;
    double nearest=std::numeric_limits<double>::infinity();uint64_t hit=0;
    for(const auto& t:targets)if(t.id&&!t.self) {
        auto d=distance(t,self);if(!d||*d>rangeMeters)continue;
        // Approximate body sphere centered 0.9 m above the displayed feet.
        // This is inspection picking, not mesh occlusion or combat reach.
        double x=double(origin.x)-t.position.x,y=double(origin.y)-t.position.y-.9,z=double(origin.z)-t.position.z;
        double b=x*nx+y*ny+z*nz,c=x*x+y*y+z*z-1.25*1.25,disc=b*b-c;
        if(disc<0)continue;
        double near=-b-std::sqrt(disc),far=-b+std::sqrt(disc);
        double intersection=near>=0?near:far;
        if(intersection<0)continue;
        if(intersection<nearest||(intersection==nearest&&(!hit||t.id<hit))){nearest=intersection;hit=t.id;}
    }
    id_=hit;
}
std::u16string targetDisplayName(const std::u16string& name) {
    std::u16string result;size_t points=0;
    for(size_t i=0;i<name.size()&&points<64;++i) {
        auto c=name[i];
        // SWG names can contain inline RGB color and reset tokens. Keep the
        // actual text (including titles), but do not print those tokens.
        if(c==u'\\'&&i+2<name.size()&&name[i+1]==u'#') {
            if(name[i+2]==u'.'){i+=2;continue;}
            if(i+7<name.size()&&std::all_of(name.begin()+i+2,name.begin()+i+8,[](char16_t h){
                return (h>=u'0'&&h<=u'9')||(h>=u'a'&&h<=u'f')||(h>=u'A'&&h<=u'F');
            })){i+=7;continue;}
        }
        if(c<0x20||(c>=0x7f&&c<0xa0)||c==0x2028||c==0x2029)continue;
        if(c>=0xd800&&c<=0xdbff) {
            if(i+1<name.size()&&name[i+1]>=0xdc00&&name[i+1]<=0xdfff){result+=c;result+=name[++i];}
            else result+=char16_t(0xfffd);
        }else result+=c>=0xdc00&&c<=0xdfff?char16_t(0xfffd):c;
        ++points;
    }
    return result.empty()?u"Unnamed creature":result;
}
std::wstring targetWideText(const std::u16string& text) {
    std::wstring result;
    if constexpr(sizeof(wchar_t)==2)for(auto c:text)result+=wchar_t(c);
    else for(size_t i=0;i<text.size();++i) {
        uint32_t c=text[i];
        if(c>=0xd800&&c<=0xdbff&&i+1<text.size()&&text[i+1]>=0xdc00&&text[i+1]<=0xdfff)
            c=0x10000+((c-0xd800)<<10)+(text[++i]-0xdc00);
        else if(c>=0xd800&&c<=0xdfff)c=0xfffd;
        result+=wchar_t(c);
    }
    return result;
}
std::string targetDistanceText(std::optional<float> distance) {
    if(!distance||!std::isfinite(*distance)||*distance<0)return {};
    std::ostringstream out;out.imbue(std::locale::classic());
    out<<std::fixed<<std::setprecision(1)<<*distance<<" m";
    return out.str();
}
} // namespace clientcommon
