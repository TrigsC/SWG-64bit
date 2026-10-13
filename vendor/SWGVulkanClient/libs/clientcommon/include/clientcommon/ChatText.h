#pragma once
#include <algorithm>
#include "ChatLog.h"
#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>
namespace clientcommon {
// Runtime-correlated neutral, language-ID-1, untargeted speech only.
// Unknown names, moods, languages and directed speech remain literal.
inline std::u16string formatNeutralSpatialChat(const ChatLine& line,const std::u16string& sender,const std::u16string& verb,uint16_t sayType){
    if(line.kind!=ChatLine::Kind::Spatial||line.chatType!=sayType||line.mood||line.flags||line.language!=1||line.target||sender.empty()||sender.size()>96||verb.empty())return line.text;
    return sender+u" "+verb+u", \""+line.text+u"\"";
}
struct ChatCharacter {uint32_t code=0;std::array<float,4> color{1,1,1,1};};
using ChatTextLine=std::vector<ChatCharacter>;
inline std::vector<ChatTextLine> wrapChatText(const std::u16string& text,int width,const std::function<int(uint32_t)>& advance,
        std::array<float,4> base,const std::map<std::string,std::array<float,4>>& palette={},std::vector<int>* measuredWidths=nullptr) {
    std::vector<ChatTextLine> lines(1);std::vector<int> widths;auto color=base;int used=0;size_t lastBreak=0;
    auto hex=[](char16_t c){if(c>='0'&&c<='9')return int(c-'0');if(c>='a'&&c<='f')return int(c-'a'+10);if(c>='A'&&c<='F')return int(c-'A'+10);return -1;};
    auto append=[&](uint32_t code) {
        const int n=advance(code);auto& line=lines.back();
        if(used+n>std::max(1,width)&&!line.empty()) {
            if(lastBreak){ChatTextLine tail(line.begin()+lastBreak,line.end());line.erase(line.begin()+lastBreak,line.end());
                int measured=0;for(const auto& c:line)measured+=advance(c.code);
                if(!line.empty()&&line.back().code==' ')measured-=advance(' '); // Original breaker_width excludes its wrapping space.
                widths.push_back(measured);lines.push_back(std::move(tail));}
            else {widths.push_back(used);lines.emplace_back();}
            used=0;lastBreak=0;for(size_t j=0;j<lines.back().size();++j){used+=advance(lines.back()[j].code);auto c=lines.back()[j].code;if(c==' '||c=='-'||c==','||c==';')lastBreak=j+1;}
        }
        lines.back().push_back({code,color});used+=n;
        if(code==' '||code=='-'||code==','||code==';')lastBreak=lines.back().size();
    };
    for(size_t i=0;i<text.size();) {
        if(text[i]=='\\'&&i+2<text.size()&&text[i+1]=='#') {
            if(text[i+2]=='.'){color=base;i+=3;continue;}
            if(i+8<=text.size()) {
                uint32_t rgb=0;bool valid=true;for(size_t j=0;j<6;++j){int h=hex(text[i+2+j]);if(h<0){valid=false;break;}rgb=(rgb<<4)|h;}
                if(valid){color={float((rgb>>16)&255)/255,float((rgb>>8)&255)/255,float(rgb&255)/255,1};i+=8;continue;}
            }
            if(text[i+2]=='p') {
                size_t end=i+3;std::string key;
                while(end<text.size()&&end-i<64&&text[end]!=' '&&text[end]<128){char c=char(text[end++]);if(c>='A'&&c<='Z')c+='a'-'A';key+=c;}
                if(end<text.size()&&text[end]==' ')if(auto found=palette.find(key);found!=palette.end()){color=found->second;i=end+1;continue;}
            }
        }
        uint32_t c=text[i++];if(c=='\r')continue;
        if(c=='\n'){widths.push_back(used);lines.emplace_back();used=0;lastBreak=0;continue;}
        if(c=='\t'){for(int n=0;n<4;++n)append(' ');continue;}
        if(c<32||c==127)continue;
        if(c>=0xd800&&c<=0xdbff&&i<text.size()&&text[i]>=0xdc00&&text[i]<=0xdfff)c=0x10000+((c-0xd800)<<10)+(text[i++]-0xdc00);
        else if(c>=0xd800&&c<=0xdfff)c=0xfffd;
        append(c);
    }
    widths.push_back(used);if(measuredWidths)*measuredWidths=std::move(widths);return lines;
}
}
