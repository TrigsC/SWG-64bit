#include "OriginalHudAssets.h"
#include "clientcommon/CreatureIndicators.h"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace dummyclient {
namespace {
[[noreturn]] void fail(const std::string& reason) { throw std::runtime_error("Original HUD assets: " + reason); }
std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) if (c >= 'A' && c <= 'Z') c = char(c + ('a' - 'A'));
    return out;
}
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
struct Node {
    std::string type;
    std::map<std::string,std::string> attrs;
    std::vector<std::unique_ptr<Node>> children;
    const std::string& attr(const std::string& key) const {
        auto i = attrs.find(lower(key));
        if (i == attrs.end()) fail("missing property " + key);
        return i->second;
    }
    std::string optional(const std::string& key, const std::string& fallback = {}) const {
        auto i = attrs.find(lower(key)); return i == attrs.end() ? fallback : i->second;
    }
};
std::string entityText(std::string_view s) {
    std::string result;
    for (size_t i = 0; i < s.size();) {
        if (s[i] != '&') { result += s[i++]; continue; }
        auto end = s.find(';', i);
        if (end == std::string_view::npos || end - i > 10) fail("unsupported property entity");
        auto e = s.substr(i, end - i + 1);
        if (e == "&amp;") result += '&'; else if (e == "&lt;") result += '<';
        else if (e == "&gt;") result += '>'; else if (e == "&apos;") result += '\'';
        else if (e == "&quot;") result += '"'; else fail("unsupported property entity");
        i = end + 1;
    }
    return result;
}
// SWG's UI format permits numeric and '+' property names, so strict XML
// parsers reject real shipped files. Read only tags and quoted properties.
std::unique_ptr<Node> parseUi(const std::vector<uint8_t>& bytes) {
    if (bytes.empty() || bytes.size() > 8 * 1024 * 1024) fail("UI file exceeds size bound");
    std::string_view s(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (s.find('\0') != std::string_view::npos) fail("NUL in UI file");
    size_t pos = 0, count = 0;
    std::unique_ptr<Node> root;
    std::vector<Node*> stack;
    auto skip = [&] { while (pos < s.size() && space(s[pos])) ++pos; };
    auto token = [&]() {
        size_t start = pos;
        while (pos < s.size() && !space(s[pos]) && s[pos] != '=' && s[pos] != '/' && s[pos] != '>') ++pos;
        if (pos == start || pos - start > 256) fail("invalid UI token");
        return lower(s.substr(start, pos - start));
    };
    while (pos < s.size()) {
        auto start = s.find('<', pos);
        if (start == std::string_view::npos) break;
        pos = start;
        if (s.substr(pos,4) == "<!--") {
            auto end = s.find("-->", pos + 4); if (end == std::string_view::npos) fail("unterminated UI comment");
            pos = end + 3; continue;
        }
        if (s.substr(pos,2) == "<?") {
            auto end = s.find("?>", pos + 2); if (end == std::string_view::npos) fail("unterminated UI declaration");
            pos = end + 2; continue;
        }
        ++pos;
        if (pos < s.size() && s[pos] == '!') fail("unsupported UI declaration");
        bool closing = pos < s.size() && s[pos] == '/'; if (closing) ++pos;
        skip(); auto type = token(); skip();
        if (closing) {
            if (pos == s.size() || s[pos++] != '>' || stack.empty() || stack.back()->type != type) fail("mismatched UI close tag");
            stack.pop_back(); continue;
        }
        if (++count > 50000 || stack.size() >= 64) fail("UI tree exceeds bound");
        auto node = std::make_unique<Node>(); node->type = type;
        while (pos < s.size() && s[pos] != '/' && s[pos] != '>') {
            if (node->attrs.size() >= 512) fail("UI property count exceeds bound");
            auto key = token(); skip();
            if (pos == s.size() || s[pos++] != '=') fail("missing UI property equals");
            skip();
            if (pos == s.size() || (s[pos] != '\'' && s[pos] != '"')) fail("unquoted UI property");
            char quote = s[pos++]; size_t begin = pos;
            auto end = s.find(quote, pos);
            if (end == std::string_view::npos || end - begin > 262144) fail("UI property exceeds bound");
            if (!node->attrs.emplace(key,entityText(s.substr(begin,end-begin))).second) fail("duplicate UI property");
            pos = end + 1; skip();
        }
        bool single = pos < s.size() && s[pos] == '/'; if (single) ++pos;
        if (pos == s.size() || s[pos++] != '>') fail("unterminated UI tag");
        Node* raw = node.get();
        if (stack.empty()) { if (root) fail("multiple UI roots"); root = std::move(node); }
        else stack.back()->children.push_back(std::move(node));
        if (!single) stack.push_back(raw);
    }
    if (!root || !stack.empty()) fail("incomplete UI tree");
    return root;
}
const Node& child(const Node& n, const std::string& name) {
    const Node* found = nullptr;
    for (const auto& c : n.children) if (lower(c->optional("name")) == lower(name)) {
        if (found) fail("ambiguous UI property path " + name); found = c.get();
    }
    if (!found) fail("missing UI property path " + name);
    return *found;
}
const Node& path(const Node& root, std::string value) {
    for (char& c : value) if (c == '.') c = '/';
    const Node* n = &root; size_t pos = 0; bool first = true;
    while (pos < value.size()) {
        if (value[pos] == '/') { ++pos; continue; }
        auto end = value.find('/',pos); if (end == std::string::npos) end = value.size();
        auto part = value.substr(pos,end-pos);
        if (first && lower(part) == lower(root.optional("name"))) { first = false; pos = end; continue; }
        n = &child(*n,part); first = false; pos = end;
    }
    return *n;
}
std::vector<uint8_t> required(const HudAssetReader& read, const std::string& name) {
    auto b = read(name); if (!b) fail("missing " + name); return std::move(*b);
}
int integer(std::string_view v, int min = -16384, int max = 16384) {
    while (!v.empty() && space(v.front())) v.remove_prefix(1);
    while (!v.empty() && space(v.back())) v.remove_suffix(1);
    int result = 0; auto parsed = std::from_chars(v.data(),v.data()+v.size(),result);
    if (v.empty() || parsed.ec != std::errc{} || parsed.ptr != v.data()+v.size() || result < min || result > max) fail("invalid bounded integer");
    return result;
}
std::vector<int> numbers(std::string_view value, size_t count) {
    std::vector<int> result; size_t pos = 0;
    do {
        auto end = value.find(',',pos); if (end == std::string_view::npos) end = value.size();
        result.push_back(integer(value.substr(pos,end-pos))); pos = end + 1;
        if (result.size() > count) fail("wrong coordinate count");
    } while (pos <= value.size());
    if (result.size() != count) fail("wrong coordinate count");
    return result;
}
HudRect rect(const std::string& s) {
    auto r = numbers(s,4);
    if (r[0] == r[2] || r[1] == r[3] || *std::min_element(r.begin(),r.end()) < 0) fail("empty or negative source rectangle");
    return {r[0],r[1],r[2],r[3]};
}
std::array<float,4> color(const Node& node, const Node& palette, const std::string& pal, const std::string& raw) {
    auto name = node.optional(pal); auto text = name.empty() ? node.attr(raw) : palette.attr(name);
    if (text.size() != 7 || text[0] != '#') fail("invalid RGB color");
    unsigned value = 0; auto p = std::from_chars(text.data()+1,text.data()+7,value,16);
    if (p.ec != std::errc{} || p.ptr != text.data()+7) fail("invalid RGB color");
    return {float((value>>16)&255)/255.f,float((value>>8)&255)/255.f,float(value&255)/255.f,1.f};
}
float opacity(const std::string& text) {
    char* end=nullptr;float value=std::strtof(text.c_str(),&end);
    if (end==text.c_str() || end!=text.c_str()+text.size() || !std::isfinite(value) || value<0 || value>1) fail("invalid opacity");
    return value;
}
bool boolean(const std::string& text) {
    auto value=lower(text);if(value=="true")return true;if(value=="false")return false;fail("invalid style boolean");
}
HudWidgetLayout layout(const Node& node) {
    auto location=numbers(node.optional("location","0,0"),2),size=numbers(node.attr("size"),2);
    if(size[0]<=0 || size[1]<=0)fail("invalid widget dimensions");
    HudWidgetLayout result{location[0],location[1],size[0],size[1],node.optional("packlocation"),node.optional("packsize")};
    result.hasMinimumSize=!node.optional("minimumsize").empty();result.hasMaximumSize=!node.optional("maximumsize").empty();
    auto min=numbers(node.optional("minimumsize","0,0"),2),max=numbers(node.optional("maximumsize","16384,16384"),2);
    if(min[0]<0 || min[1]<0 || max[0]<min[0] || max[1]<min[1])fail("invalid widget size constraints");
    result.minimumWidth=min[0];result.minimumHeight=min[1];result.maximumWidth=max[0];result.maximumHeight=max[1];
    return result;
}
HudPageAppearance appearance(const Node& node,const Node& palette) {
    HudPageAppearance result;
    result.hasBackgroundColor=!node.optional("backgroundcolor").empty();
    result.hasBackgroundTint=!node.optional("backgroundtint").empty() || !node.optional("palbgtint").empty();
    result.hasColor=!node.optional("color").empty() || !node.optional("palcolor").empty();
    result.hasBackgroundOpacity=!node.optional("backgroundopacity").empty();
    if(result.hasBackgroundColor)result.backgroundColor=color(node,palette,"","backgroundcolor");
    if(result.hasBackgroundTint)result.backgroundTint=color(node,palette,"palbgtint","backgroundtint");
    if(result.hasColor)result.color=color(node,palette,"palcolor","color");
    if(result.hasBackgroundOpacity)result.backgroundOpacity=opacity(node.attr("backgroundopacity"));
    return result;
}
std::string texturePath(const std::string& name) {
    if (name.empty() || name.size() > 256 || name.front() == '/' || name.find(':') != std::string::npos || name.find("..") != std::string::npos) fail("unsupported UI texture path");
    for (char c : name) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '/' || c == '.')) fail("invalid UI texture path");
    auto p = name.starts_with("texture/") ? name : "texture/" + name;
    if (!p.ends_with(".dds")) p += ".dds"; return p;
}
HudRectangleAsset rectangleAsset(const Node& styles,const Node& palette,const Node& widget,bool requireAllSlices) {
    static const std::pair<const char*,const char*> parts[]={{"nw","NorthWest"},{"ne","NorthEast"},{"sw","SouthWest"},{"se","SouthEast"},{"n","North"},{"s","South"},{"w","West"},{"e","East"},{"c","Center"}};
    auto fp=widget.attr("rstyledefault");auto last=fp.find_last_of("/.");
    if(last==std::string::npos)fail("invalid rectangle property path");auto base=fp.substr(0,last);
    const auto& rectangle=path(styles,fp);HudRectangleAsset result;
    if(rectangle.type!="rectanglestyle")fail("invalid rectangle style type");
    if(!rectangle.optional("color").empty() || !rectangle.optional("palcolor").empty())result.color=color(rectangle,palette,"palcolor","color");
    result.opacity=opacity(rectangle.optional("opacity","1"));
    for(const auto& [shortName,property]:parts) {
        auto value=rectangle.optional(property);
        if(value.empty()) {if(requireAllSlices)fail("missing frame slice");continue;}
        const auto& image=path(styles,base+"/"+value);
        if(image.type!="imagestyle")fail("invalid rectangle image type");
        result.slices.emplace(shortName,HudImageSlice{texturePath(image.attr("source")),rect(image.attr("sourcerect"))});
    }
    if(!result.slices.contains("c"))fail("missing rectangle center");
    result.stretchCenterH=boolean(rectangle.optional("CenterStretchH","true"));result.stretchCenterV=boolean(rectangle.optional("CenterStretchV","true"));
    result.stretchNorth=boolean(rectangle.optional("NorthStretch","true"));result.stretchSouth=boolean(rectangle.optional("SouthStretch","true"));
    result.stretchWest=boolean(rectangle.optional("WestStretch","true"));result.stretchEast=boolean(rectangle.optional("EastStretch","true"));
    result.shrinkCenterH=boolean(rectangle.optional("CenterShrinkH","true"));result.shrinkCenterV=boolean(rectangle.optional("CenterShrinkV","true"));
    result.shrinkNorth=boolean(rectangle.optional("NorthShrink","true"));result.shrinkSouth=boolean(rectangle.optional("SouthShrink","true"));
    result.shrinkWest=boolean(rectangle.optional("WestShrink","true"));result.shrinkEast=boolean(rectangle.optional("EastShrink","true"));
    return result;
}
HudHamLayout hamLayout(const Node& status,const Node& styles,const Node& palette) {
    const auto& composite=child(status,"composite");const auto& ham=child(composite,"ham");
    HudHamLayout result;result.composite=layout(composite);result.ham=layout(ham);
    for(const auto& node:ham.children)if(lower(node->optional("name"))=="f")result.hasAuthoredForceRow=true;
    auto layer=[&](const Node& node) {return HudBarLayer{layout(node),appearance(node,palette),rectangleAsset(styles,palette,node,false)};};
    for(const auto* key:{"h","a","m","f"}) {
        if(std::string(key)=="f"&&!result.hasAuthoredForceRow)continue;
        const auto& row=child(ham,key);const auto& normal=child(row,"normalmax");const auto& inner=child(normal,"inner");
        HudHamRow item;item.layout=layout(row);item.inner=layout(inner);item.innerAppearance=appearance(inner,palette);
        item.normalMax=layer(normal);item.current=layer(child(inner,"current"));
        // Original Force CodeData has no currentMax/recentMax pages.
        if(std::string(key)!="f"){
            item.currentMax=layer(child(inner,"currentmax"));item.recentMax=layer(child(inner,"recentmax"));item.hasRecentMaximum=true;
        }
        item.recentCurrent=layer(child(inner,"recentcurrent"));
        result.rows.emplace(key,std::move(item));
    }
    return result;
}
HudPostureWidget postureWidget(const Node& posture,const Node& palette) {
    const auto& icon=child(posture,"icon");HudPostureWidget result;
    result.layout=layout(icon);result.color=color(icon,palette,"palcolor","color");
    if(lower(result.layout.packLocation)!="cfc,cfc"||lower(result.layout.packSize)!="f,f"||
       !boolean(icon.optional("stretch","true"))||result.layout.width<=0||result.layout.height<=0)
        fail("unsupported posture icon packing");
    return result;
}
HudImageStyle imageStyle(const Node& node,bool fallback) {
    if(node.type!="imagestyle")fail("invalid posture image style");
    HudImageStyle result;result.fallback=fallback;
    auto frame=[](const Node& item) {
        HudImageFrame f;f.image={texturePath(item.attr("source")),rect(item.attr("sourcerect"))};
        auto offset=numbers(item.optional("offset","0,0"),2),proportion=numbers(item.optional("offsetproportional","0,0"),2);
        f.offsetX=offset[0];f.offsetY=offset[1];f.proportionalX=proportion[0];f.proportionalY=proportion[1];
        f.duration=uint32_t(integer(item.optional("duration","1"),1,65536));return f;
    };
    if(node.children.empty())result.frames.push_back(frame(node));
    else {
        if(node.children.size()>64)fail("posture animation frame budget exceeded");
        for(const auto& item:node.children) {
            if(item->type!="imageframe")fail("unsupported posture animation record");
            result.frames.push_back(frame(*item));
        }
        // This subset covers the shipped fallback animation. Refuse changes
        // to timing instead of silently inventing an animation controller.
        if(node.optional("playtime","0.5")!="0.50"&&node.optional("playtime","0.5")!="0.5")
            fail("unsupported posture animation playtime");
        result.looping=boolean(node.optional("loop","true"));result.reversing=boolean(node.optional("reversing","false"));
    }
    return result;
}
HudStateVolume stateVolume(const Node& status,const Node& palette) {
    HudStateVolume out;const auto& composite=child(status,"composite");const auto& volume=child(composite,"vs");
    out.composite=layout(composite);out.volume=layout(volume);
    const auto size=numbers(volume.attr("CellSize"),2),padding=numbers(volume.optional("CellPadding","0,0"),2);
    if(size[0]<=0||size[1]<=0||padding[0]||padding[1]||lower(volume.optional("CellAlignmentH","left"))!="left")fail("unsupported state cell packing");
    out.cellWidth=size[0];out.cellHeight=size[1];
    const auto& sample=child(status,"sampleStateIcon");
    if(sample.attrs.contains("color")||sample.attrs.contains("palcolor"))out.sampleColor=color(sample,palette,"PalColor","Color");
    return out;
}
uint32_t u32(const std::vector<uint8_t>& b, size_t at) {
    if (at > b.size() || b.size()-at < 4) fail("truncated binary resource");
    return uint32_t(b[at]) | uint32_t(b[at+1])<<8 | uint32_t(b[at+2])<<16 | uint32_t(b[at+3])<<24;
}

std::map<std::string,std::u16string> parseStf(const std::vector<uint8_t>& b) {
    if (b.size() < 13 || b.size() > 8*1024*1024 || u32(b,0) != 0xabcd || b[4] != 1) fail("unsupported STF header");
    auto count = u32(b,9); if (count > 65536) fail("STF entries exceed bound");
    size_t pos = 13; std::map<uint32_t,std::u16string> values;
    for (uint32_t i = 0; i < count; ++i) {
        auto id = u32(b,pos), length = u32(b,pos+8); pos += 12;
        if (length > 65536 || pos > b.size() || size_t(length)*2 > b.size()-pos) fail("truncated STF value");
        std::u16string value; value.reserve(length);
        for (uint32_t j = 0; j < length; ++j) value += char16_t(uint16_t(b[pos+j*2]) | uint16_t(b[pos+j*2+1])<<8);
        if (!values.emplace(id,std::move(value)).second) fail("duplicate STF value");
        pos += size_t(length)*2;
    }
    std::map<std::string,std::u16string> result;
    for (uint32_t i = 0; i < count; ++i) {
        auto id = u32(b,pos), length = u32(b,pos+4); pos += 8;
        if (length > 1024 || pos > b.size() || length > b.size()-pos) fail("truncated STF key");
        std::string_view name(reinterpret_cast<const char*>(b.data()+pos),length);
        auto value=values.find(id);if(value==values.end()||!result.emplace(std::string(name),value->second).second)fail("invalid STF label mapping");
        pos += length;
    }
    if (pos != b.size()) fail("trailing STF data"); return result;
}
std::optional<std::u16string> labelStf(const std::vector<uint8_t>& b,const std::string& key){
    auto table=parseStf(b);auto found=table.find(key);return found==table.end()?std::optional<std::u16string>{}:found->second;
}
}

std::optional<size_t> HudImageStyle::frameAt(uint64_t tick) const {
    if(frames.empty())return {};
    if(frames.size()==1)return 0;
    // Pinned UIImageStyle::GetFrame: UI animation time is in 60Hz ticks,
    // duration boundaries are strict '>'. Preserve the reversal endpoint.
    float seconds=float(tick)/60.f;
    if(seconds>=playTime) {
        if(reversing||looping)seconds=std::fmod(seconds,playTime);
        else return frames.size()-1;
    }
    uint32_t total=0;for(const auto& f:frames)total+=f.duration;
    uint32_t desired=0;
    if(reversing&&frames.size()>2) {
        const float length=1.f+float(frames.size()-2)/float(frames.size());
        desired=uint32_t(seconds*length/playTime*float(total));
        if(desired>total)desired=uint32_t(length*float(total)-desired);
    } else if(looping)desired=uint32_t(seconds/playTime*float(total));
    total=0;for(size_t i=0;i<frames.size();++i) {total+=frames[i].duration;if(total>desired)return i;}
    return {};
}

std::map<std::string,std::u16string> decodeOriginalStringTable(const std::vector<uint8_t>& bytes){return parseStf(bytes);}

OriginalHudAssets OriginalHudAssets::load(const HudAssetReader& reader) {
    if (!reader) fail("missing asset reader");
    auto root = parseUi(required(reader,"ui/ui_root.ui"));
    auto ground = parseUi(required(reader,"ui/ui_ground_hud.inc"));
    auto styles = parseUi(required(reader,"ui/ui_styles.inc"));
    auto effectors = parseUi(required(reader,"ui/ui_effectors.inc"));
    auto managers = parseUi(required(reader,"ui/ui_textstylemanager.inc"));
    const auto& palette = path(*styles,root->attr("palette"));
    const auto& target = child(*ground,"Target");
    const auto& status = child(target,"status");
    const auto& name = child(status,"textName");
    const auto& label = child(status,"label");
    const auto& back = child(status,"back");
    const auto& frame = path(*styles,back.attr("RStyleDefault"));
    OriginalHudAssets out;
    auto size = numbers(target.attr("size"),2), loc = numbers(target.attr("location"),2);
    if (size[0] <= 0 || size[1] <= 0) fail("invalid target frame size");
    // Only the shipped top-right target packing is implemented by this slice.
    if (lower(target.attr("PackLocation")) != "fff,nfn") fail("unsupported target packing");
    out.targetWidth = size[0]; out.targetHeight = size[1]; out.targetOffsetY = loc[1];
    auto rootSize = numbers(ground->attr("size"),2);
    if (rootSize[0] <= 0 || lower(target.attr("PackSize")) != "p,f") fail("unsupported target size packing");
    out.targetPackWidthRatio = float(size[0])/float(rootSize[0]);
    auto fl = numbers(back.optional("location","0,0"),2), fs = numbers(back.attr("size"),2);
    if (fs[0] <= 0 || fs[1] <= 0) fail("invalid target background size");
    out.targetFrameX=fl[0]; out.targetFrameY=fl[1]; out.targetFrameWidth=fs[0]; out.targetFrameHeight=fs[1];
    auto nl = numbers(name.attr("location"),2), ns = numbers(name.attr("size"),2);
    auto ll = numbers(label.attr("location"),2), ls = numbers(label.attr("size"),2);
    if (ns[0] <= 0 || ns[1] <= 0 || ls[0] <= 0 || ls[1] <= 0) fail("invalid target text size");
    out.targetNameX=nl[0]; out.targetNameY=nl[1]; out.targetNameWidth=ns[0]; out.targetNameHeight=ns[1];
    out.targetLabelX=ll[0]; out.targetLabelY=ll[1]; out.targetLabelWidth=ls[0]; out.targetLabelHeight=ls[1];
    out.nameColor=color(name,palette,"PalText","TextColor");
    out.labelColor=color(label,palette,"PalText","TextColor");
    out.frameColor=color(frame,palette,"PalColor","Color");
    // RectangleStyle references are relative to its containing namespace.
    auto readFrame = [&](const Node& widget,std::map<std::string,HudImageSlice>& slices) {
        slices=rectangleAsset(*styles,palette,widget,true).slices;
    };
    readFrame(back,out.frameSlices);
    const auto& posture=child(child(status,"vsp"),"posture");
    auto pl=numbers(posture.attr("location"),2), ps=numbers(posture.attr("size"),2);
    if (ps[0]<=0 || ps[1]<=0) fail("invalid posture background size");
    out.targetPostureFrameX=pl[0]; out.targetPostureFrameY=pl[1]; out.targetPostureFrameWidth=ps[0]; out.targetPostureFrameHeight=ps[1];
    readFrame(posture,out.postureFrameSlices);
    out.targetHam=hamLayout(status,*styles,palette);
    out.targetPostureIcon=postureWidget(posture,palette);
    const auto& self=child(*ground,"mfdstatus");const auto& selfName=child(self,"textname");
    if(lower(self.attr("PackLocation"))!="nfn,nfn" || lower(self.attr("PackSize"))!="p,f")fail("unsupported self status packing");
    auto sl=layout(self),sfl=layout(child(self,"back")),snl=layout(selfName);
    auto spl=layout(child(child(self,"vsp"),"posture"));
    out.selfWidth=sl.width;out.selfHeight=sl.height;out.selfOffsetX=sl.x;out.selfOffsetY=sl.y;
    out.selfPackWidthRatio=float(sl.width)/float(rootSize[0]);
    out.selfFrameX=sfl.x;out.selfFrameY=sfl.y;out.selfFrameWidth=sfl.width;out.selfFrameHeight=sfl.height;
    out.selfPostureFrameX=spl.x;out.selfPostureFrameY=spl.y;out.selfPostureFrameWidth=spl.width;out.selfPostureFrameHeight=spl.height;
    out.selfNameX=snl.x;out.selfNameY=snl.y;out.selfNameWidth=snl.width;out.selfNameHeight=snl.height;
    out.selfNameAlignmentVertical=selfName.optional("TextAlignmentVertical","Top");
    out.selfNameHorizontalAlignmentAuthored=!selfName.optional("TextAlignment").empty();
    out.selfNameAlignmentHorizontal=selfName.optional("TextAlignment","Left"); // published UIText constructor default
    out.selfNameColor=color(selfName,palette,"PalText","TextColor");
    auto selfFrame=rectangleAsset(*styles,palette,child(self,"back"),true);
    auto selfPostureFrame=rectangleAsset(*styles,palette,child(child(self,"vsp"),"posture"),true);
    out.selfFrameColor=selfFrame.color;out.selfFrameSlices=std::move(selfFrame.slices);
    out.selfPostureFrameColor=selfPostureFrame.color;out.selfPostureFrameSlices=std::move(selfPostureFrame.slices);
    out.selfHam=hamLayout(self,*styles,palette);
    out.selfPostureIcon=postureWidget(child(child(self,"vsp"),"posture"),palette);
    const auto& bank=path(*styles,"/Styles/Icon/posture");
    const auto fallback=imageStyle(path(*styles,"/Styles/Icon/fallback"),true);
    for(size_t i=0;i<out.postureIcons.size();++i) {
        const Node* match=nullptr;
        for(const auto& item:bank.children)if(lower(item->optional("name"))==lower(clientcommon::postureNames[i])) {match=item.get();break;}
        out.postureIcons[i]=match?imageStyle(*match,false):fallback;
    }
    out.selfStates=stateVolume(self,palette);out.targetStates=stateVolume(status,palette);
    // UIPacking centers the odd-height volume using integer division. The
    // horizontal Fill split is authored HAM width115 and state width115/114.
    out.selfStates.volume.x=out.selfHam.ham.width;
    out.targetStates.volume.x=0;
    for(auto* v:{&out.selfStates,&out.targetStates})
        v->volume.y=v->composite.height/2-v->volume.height/2;
    const auto& stateBank=path(*styles,"/Styles/Icon/state");
    const auto& fallbackNode=path(*styles,"/Styles/Icon/fallback");
    for(size_t i=0;i<out.stateIcons.size();++i) {
        const Node* match=nullptr;
        for(const auto& item:stateBank.children)if(lower(item->optional("name"))==lower(clientcommon::stateIconNames[i])){match=item.get();break;}
        out.stateIcons[i]=match?imageStyle(*match,false):fallback;
        const auto& selected=match?*match:fallbackNode;
        if(selected.attrs.contains("color"))out.stateColors[i]=color(selected,palette,"PalColor","Color");
    }
    const auto& fade=path(*effectors,"/Effectors/opacity/fast/FadeFull");
    if(fade.attr("Speed")!="8.00"||fade.attr("TargetOpacity")!="1.00")fail("unsupported state fade");
    out.stateFadeSpeed=8.f;
    const Node* manager = nullptr;
    for (const auto& node : managers->children) if (node->type == "textstylemanager") {
        if (manager) fail("ambiguous font manager"); manager = node.get();
    }
    if (!manager) fail("missing font manager");
    auto selector = lower(name.attr("font")); auto separator = selector.find_last_of('_');
    if (separator == std::string::npos || selector != lower(label.attr("font")) || selector!=lower(selfName.attr("font"))) fail("unsupported status font selectors");
    auto face = manager->attr("fontfaces_en_"+selector.substr(0,separator));
    integer(selector.substr(separator+1),1,128);
    auto fontName = face + selector.substr(separator);
    auto fontRelative = "font/" + fontName + ".inc";
    if (fontRelative.find("..") != std::string::npos || fontRelative.find(':') != std::string::npos || face.find('/') != std::string::npos) fail("invalid font face path");
    // Fonts_en is the shipped allowlist; resolve the named font through it.
    auto list = manager->attr("fonts_en"); bool included = false;
    for (size_t i=0;i<list.size();) {
        while (i<list.size() && space(list[i])) ++i; size_t begin=i;
        while (i<list.size() && !space(list[i])) ++i;
        if (lower(std::string_view(list).substr(begin,i-begin)) == lower(fontRelative)) included=true;
    }
    if (!included) fail("target font absent from English font allowlist");
    auto font = parseUi(required(reader,"ui/"+fontRelative));
    if (font->type != "textstyle" || lower(font->attr("name")) != lower(fontName)) fail("wrong font asset");
    out.fontLeading=integer(font->attr("leading"),1,128);
    if (font->children.empty() || font->children.size() > 8192) fail("font glyph count exceeds bound");
    for (const auto& glyph : font->children) {
        if (glyph->type != "fontcharacter") fail("unsupported font record");
        HudGlyph item;
        item.code=uint32_t(integer(glyph->optional("code","0"),0,0x10ffff));
        if (item.code>=0xd800 && item.code<=0xdfff) fail("surrogate font code point");
        item.advance=integer(glyph->attr("advance"),0,512);
        item.advancePre=integer(glyph->optional("advancepre","0"),-512,512);
        item.source=rect(glyph->attr("sourcerect"));
        item.texturePath=texturePath(glyph->attr("sourcefile"));
        if (!out.glyphs.emplace(item.code,std::move(item)).second) fail("duplicate font code point");
    }
    if (!out.glyphs.contains(0)) fail("missing font fallback glyph");
    auto labelKey=label.attr("localtext");
    if (labelKey != "[@ui:groundhud_target]") fail("unsupported target label resource");
    if (auto bytes=reader("string/en/ui.stf")) {
        auto text=labelStf(*bytes,"groundhud_target");
        if (!text || text->empty() || text->size()>128) fail("missing or invalid target localization");
        out.targetLabel=std::move(*text); out.targetLabelFromStf=true;
    } else {
        // Verified from the extracted English ui.stf, entry253; explicit fallback.
        out.targetLabel=u"Target:";
    }
    return out;
}

OriginalChatAssets OriginalChatAssets::load(const HudAssetReader& reader) {
    auto root=parseUi(required(reader,"ui/ui_root.ui")),chat=parseUi(required(reader,"ui/ui_ground_hud_chat_window.inc"));
    auto styles=parseUi(required(reader,"ui/ui_styles.inc")),managers=parseUi(required(reader,"ui/ui_textstylemanager.inc"));
    const auto& palette=path(*styles,root->attr("palette"));OriginalChatAssets out;
    for(const auto& [key,value]:palette.attrs)if(value.size()==7&&value[0]=='#') {
        Node n;n.attrs["color"]=value;out.palette[key]=color(n,palette,"PalColor","Color");
    }
    std::function<void(const Node&,int,std::string)> visit=[&](const Node& node,int parent,std::string full) {
        if(node.type=="data"||node.type=="datasource"||!node.attrs.contains("size"))return;
        if(out.nodes.size()>128)fail("chat widget budget");
        full+="/"+lower(node.optional("name",node.type+std::to_string(out.nodes.size())));const int index=int(out.nodes.size());
        HudChatNode item;item.layout=layout(node);item.parent=parent;item.path=full;
        item.appearance=appearance(node,palette);item.visible=boolean(node.optional("visible","true"));
        if(item.appearance.backgroundOpacity>0&&!node.optional("rstyledefault").empty())item.style=rectangleAsset(*styles,palette,node,false);
        out.nodes.push_back(std::move(item));
        if(full.ends_with("/collapsing/output/body/text"))out.output=index;
        if(full.ends_with("/persistent/body/input"))out.input=index;
        if(full.ends_with("/persistent/body/textchannel"))out.channel=index;
        if(full.ends_with("/collapsing/tabs"))out.tabs=index;
        for(const auto& child:node.children)visit(*child,index,full);
    };
    visit(*chat,-1,"");
    if(!out.output||!out.input||!out.channel||!out.tabs)fail("missing original chat binding");
    const auto& output=child(child(child(child(*chat,"collapsing"),"output"),"body"),"text");
    const auto& input=child(child(child(*chat,"persistent"),"body"),"input");
    const auto& channel=child(child(child(*chat,"persistent"),"body"),"textchannel");
    auto margin=numbers(output.attr("margin"),4);std::copy(margin.begin(),margin.end(),out.outputMargin.begin());
    out.outputColor=color(output,palette,"PalText","TextColor");out.channelColor=color(channel,palette,"PalText","TextColor");
    const auto& inputStyle=path(*styles,input.attr("style"));
    auto padding=numbers(inputStyle.attr("textpadding"),4);std::copy(padding.begin(),padding.end(),out.inputPadding.begin());
    out.inputColor=color(inputStyle,palette,"PalText","TextColor");out.caretColor=color(inputStyle,palette,"PalCarat","CaratColor");
    out.caretWidth=integer(inputStyle.attr("caratwidth"),1,8);
    const Node* manager=nullptr;for(const auto& child:managers->children)if(child->type=="textstylemanager")manager=child.get();
    if(!manager)fail("missing chat font manager");
    for(const auto* selector:{"default_12","default_13","bold_12","bold_13"}) {
        const std::string key=selector;auto split=key.find('_');auto face=manager->attr("fontfaces_en_"+key.substr(0,split));
        const auto name=face+key.substr(split);if(name.find('/')!=std::string::npos||name.find("..")!=std::string::npos)fail("unsafe chat font path");
        auto font=parseUi(required(reader,"ui/font/"+name+".inc"));HudBitmapFont f;f.leading=integer(font->attr("leading"),1,128);
        if(font->children.size()>8192)fail("chat font glyph budget");
        for(const auto& glyph:font->children){HudGlyph g;g.code=uint32_t(integer(glyph->optional("code","0"),0,0x10ffff));
            g.advance=integer(glyph->attr("advance"),0,512);g.advancePre=integer(glyph->optional("advancepre","0"),-512,512);
            g.source=rect(glyph->attr("sourcerect"));g.texturePath=texturePath(glyph->attr("sourcefile"));
            if(!f.glyphs.emplace(g.code,std::move(g)).second)fail("duplicate chat glyph");}
        if(!f.glyphs.contains(0))fail("missing chat font fallback");out.fonts.emplace(key,std::move(f));
    }
    if(output.attr("font")!="default_12"||inputStyle.attr("normaltextstyle")!="default_13")fail("unsupported chat font binding");
    const auto& tabPane=child(child(*chat,"collapsing"),"tabs");
    const auto panePath=tabPane.attr("style");const auto& tabStyle=path(*styles,panePath);
    const auto buttonPath=panePath.substr(0,panePath.find_last_of("/."))+"/"+tabStyle.attr("buttonstyle");
    const auto& button=path(*styles,buttonPath);const auto base=buttonPath.substr(0,buttonPath.find_last_of("/."));
    if(button.attr("normaltextstyle")!="bold_12"||lower(button.attr("textalignment"))!="left")fail("unsupported tab font/alignment");
    const char* rectangles[]={"rstyledefault","rstyleactivated","rstylemouseover"};
    const char* pal[]={"palnormaltext","palactivatedtext","palhovertext"};const char* colors[]={"normaltextcolor","pressedtextcolor","mouseovertextcolor"};
    for(size_t i=0;i<3;++i){Node widget;widget.attrs["rstyledefault"]=base+"/"+button.attr(rectangles[i]);out.tabStyles[i]=rectangleAsset(*styles,palette,widget,false);out.tabTextColors[i]=color(button,palette,pal[i],colors[i]);}
    auto tabMargin=numbers(button.attr("normaltextmargin"),4);std::copy(tabMargin.begin(),tabMargin.end(),out.tabTextMargin.begin());
    const auto strings=decodeOriginalStringTable(required(reader,"string/en/ui.stf"));
    const char* keys[]={"chat_channelid_spatial","chat_channelid_combat","chat_channelid_planet","chat_channelid_quest"};
    for(size_t i=0;i<4;++i){auto found=strings.find(keys[i]);if(found==strings.end()||found->second.size()>64)fail("missing tab label");out.tabLabels[i]=found->second;}
    return out;
}

OriginalScreenNoticeAssets OriginalScreenNoticeAssets::load(const HudAssetReader& reader){
    auto root=parseUi(required(reader,"ui/ui_root.ui")),ground=parseUi(required(reader,"ui/ui_ground_hud.inc"));
    auto styles=parseUi(required(reader,"ui/ui_styles.inc")),effectors=parseUi(required(reader,"ui/ui_effectors.inc"));
    const auto& palette=path(*styles,root->attr("palette"));const auto& page=child(*ground,"SystemMessage");
    const auto& comp=child(page,"composite");const auto& sample=child(page,"sampleText");OriginalScreenNoticeAssets out;
    out.page=layout(page);out.composite=layout(comp);out.sample=layout(sample);
    auto increment=numbers(comp.attr("sizeincrement"),2);std::copy(increment.begin(),increment.end(),out.sizeIncrement.begin());
    if(out.sizeIncrement[0]<1||out.sizeIncrement[1]<1||out.sizeIncrement[0]>128||out.sizeIncrement[1]>128)fail("Invalid notice size increment");
    auto margin=numbers(sample.attr("margin"),4);std::copy(margin.begin(),margin.end(),out.margin.begin());
    out.textColor=color(sample,palette,"PalText","TextColor");out.spacing=integer(comp.attr("spacing"),0,128);
    if(sample.attr("font")!="bold_13"||lower(sample.attr("textalignment"))!="center"||lower(sample.attr("textalignmentvertical"))!="center"||lower(comp.attr("alignment"))!="back"||lower(comp.attr("padding"))!="both"||lower(comp.attr("spacingtype"))!="constant")fail("Unsupported screen notice layout");
    if(appearance(sample,palette).backgroundOpacity!=0||appearance(page,palette).backgroundOpacity!=0)fail("Unsupported opaque notice background");
    auto speed=[&](const std::string& binding,float expected){const auto& e=path(*effectors,binding);const auto text=e.attr("speed");char* end=nullptr;float v=std::strtof(text.c_str(),&end);if(end!=text.c_str()+text.size()||!std::isfinite(v)||v<=0||v>100||opacity(e.optional("targetopacity","0"))!=expected)fail("Invalid notice fade");return v;};
    out.fadeInSpeed=speed(sample.attr("onenableeffector"),1);out.fadeOutSpeed=speed(sample.attr("ondisableeffector"),0);
    const auto& parentFade=path(*effectors,page.attr("ondisableeffector"));out.parentIdleOpacity=opacity(parentFade.attr("targetopacity"));
    out.parentFadeSpeed=speed(page.attr("ondisableeffector"),out.parentIdleOpacity);
    out.textMinimum=opacity(page.attr("textopacityrelativemin"));return out;
}

HudRgbaImage decodeHudRgbaDds(const std::vector<uint8_t>& b) {
    if (b.size() < 128 || b.size() > 72*1024*1024 || u32(b,0) != 0x20534444 || u32(b,4) != 124 || u32(b,76) != 32) fail("invalid DDS header");
    if ((u32(b,80)&0x45) != 0x41 || u32(b,84) != 0 || u32(b,88) != 32 || u32(b,112) != 0) fail("unsupported font DDS format");
    uint32_t width=u32(b,16),height=u32(b,12);
    if (!width || !height || width > 4096 || height > 4096) fail("DDS dimensions exceed bound");
    size_t pitch=(u32(b,8)&8) ? u32(b,20) : size_t(width)*4;
    if (pitch < size_t(width)*4 || pitch > size_t(width)*4+4096 || pitch*height > b.size()-128) fail("truncated or invalid DDS pitch");
    std::array<uint32_t,4> masks{u32(b,92),u32(b,96),u32(b,100),u32(b,104)};
    std::array<unsigned,4> shifts{}; uint32_t occupied=0;
    for (size_t i=0;i<4;++i) {
        if (!masks[i] || (masks[i]&occupied)) fail("invalid DDS channel masks"); occupied|=masks[i];
        auto m=masks[i]; while (!(m&1)) { ++shifts[i];m>>=1; }
        if ((m&(m+1)) != 0) fail("noncontiguous DDS channel mask");
    }
    HudRgbaImage out{int(width),int(height),std::vector<uint8_t>(size_t(width)*height*4)};
    for (size_t y=0;y<height;++y) for (size_t x=0;x<width;++x) {
        auto pixel=u32(b,128+y*pitch+x*4); auto to=(y*width+x)*4;
        for (size_t c=0;c<4;++c) {
            uint32_t maximum=masks[c]>>shifts[c],value=(pixel&masks[c])>>shifts[c];
            out.pixels[to+c]=uint8_t((uint64_t(value)*255+maximum/2)/maximum);
        }
    }
    return out;
}
}
