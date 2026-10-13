#pragma once
#include "ChatLog.h"
#include "swgproto/ChatInstantMessage.h"
#include <cwctype>
#include <algorithm>
namespace clientcommon {
inline std::string lowerTell(std::string value){for(char& c:value)if(c>='A'&&c<='Z')c+='a'-'A';return value;}
inline std::u16string shortTellName(const swgproto::ChatAvatarId& id,const swgproto::ChatAvatarId& self){
 std::string prefix;
 if(!id.game.empty()&&lowerTell(id.game)!=lowerTell(self.game))prefix=id.game+'.'+id.galaxy+'.';
 else if(!id.galaxy.empty()&&lowerTell(id.galaxy)!=lowerTell(self.galaxy))prefix=id.galaxy+'.';
 std::u16string out;for(unsigned char c:prefix+id.name)out+=char16_t(c);return out;
}
inline ChatLine tellLine(const std::string& key,const swgproto::ChatAvatarId& from,const std::u16string& body,const swgproto::ChatAvatarId& self){
 ChatLine line;line.kind=ChatLine::Kind::Instant;line.displayType=2;line.localized.emplace();auto& p=*line.localized;p.id={"ui",key};p.tu.text=shortTellName(from,self);p.to.text=body;
 // Exact shipped normal/prose style2; profile selection is a separate feature.
 if(!p.tu.text.empty())p.tu.text[0]=char16_t(std::towupper(p.tu.text[0]));
 if(!p.to.text.empty())p.to.text[0]=char16_t(std::towupper(p.to.text[0]));return line;
}
inline std::string tellResultKey(uint32_t result,bool reply){
 if(result==0)return reply?"im_success_reply_prose":"im_success_prose";
 if(result==4)return "im_recipient_offline_prose";if(result==9)return "im_recipient_ignored_prose";
 if(result==16)return "im_failed_insufficient_privs_prose";return "im_failed_unknown_prose";
}
inline std::u16string tellIncomingColor(const std::u16string& body){
 if(body.empty())return {};
 size_t at=0;auto hex=[](char16_t c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');};
 while(at+2<body.size()&&body[at]==u'\\'&&body[at+1]==u'#'){
  if(body[at+2]==u'.'){at+=3;continue;}
  if(at+8<=body.size()&&std::all_of(body.begin()+at+2,body.begin()+at+8,hex)){at+=8;continue;}
  if(body[at+2]==u'p'){auto end=body.find(u' ',at+3);if(end!=body.npos&&end-at<64){at=end+1;continue;}}
  break;
 }
 return body.substr(0,at)+u"\\#f4ef1d"+body.substr(at)+u"\\#.";
}
}
