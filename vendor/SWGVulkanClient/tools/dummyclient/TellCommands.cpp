#include "TellCommands.h"
#include <set>
#include <tuple>
namespace dummyclient {
namespace {
std::u16string trim(const std::u16string& s){auto first=s.find_first_not_of(u" \n\r\t");if(first==s.npos)return {};auto last=s.find_last_not_of(u" \n\r\t");return s.substr(first,last-first+1);}
std::optional<swgproto::ChatAvatarId> avatar(const std::u16string& token,const swgproto::ChatAvatarId& self){
 std::string raw;for(auto c:token){if(c<32||c>255)return {};raw+=char(c);}if(raw.size()>1024)return {};
 swgproto::ChatAvatarId out;const auto dot=raw.rfind('.');
 if(dot==raw.npos)out.name=raw;else {out.name=raw.substr(dot+1);if(dot>0){auto prior=raw.rfind('.',dot-1);if(prior==raw.npos)out.galaxy=raw.substr(0,dot);else {out.galaxy=raw.substr(prior+1,dot-prior-1);out.game=raw.substr(0,prior);}}}
 if(out.name.empty())return {};if(out.game.empty())out.game=self.game;if(out.galaxy.empty())out.galaxy=self.galaxy;return out;
}
}
ParsedTell parseTell(const std::u16string& input,const CommandCatalog& catalog,const clientcommon::TellSubmission::Context& context,const std::optional<std::u16string>& targetName){
 ParsedTell out;auto start=input.find_first_not_of(u" ");if(start==input.npos||input[start]!=u'/')return out;
 auto end=input.find_first_of(u" \n\r\t",start+1);if(end==input.npos)end=input.size();std::string command;
 for(auto c:input.substr(start+1,end-start-1)){if(c>127)return out;command+=char(c);}command=clientcommon::lowerTell(command);
 auto definition=catalog.definitions().find(command);if(definition==catalog.definitions().end()||!definition->second.clientOwned||definition->second.disabled)return out;
 const auto& hook=definition->second.clientHook;
 if(hook!="instantMessageTell"&&hook!="instantMessageReply"&&hook!="instantMessageRetell"&&hook!="instantMessageTtell")return out;
 out.recognized=true;if(input.size()>512){out.nativeError=u"The command is too long. It was not sent.";return out;}
 for(size_t i=0;i<input.size();++i){auto c=input[i];if(c<32||c==127){out.nativeError=u"The command contains unsupported control characters.";return out;}
  if(c>=0xd800&&c<=0xdbff){if(++i>=input.size()||input[i]<0xdc00||input[i]>0xdfff){out.nativeError=u"The command contains invalid text.";return out;}}else if(c>=0xdc00&&c<=0xdfff){out.nativeError=u"The command contains invalid text.";return out;}}
 auto args=end==input.size()?std::u16string{}:input.substr(end+1);
 if(hook=="instantMessageReply"||hook=="instantMessageRetell"){
  out.reply=true;const auto& target=hook=="instantMessageReply"?context.incoming:context.outgoing;
  if(!target){out.errorKey=hook=="instantMessageReply"?"im_no_reply_target":"im_no_retell_target";return out;}
  out.text=trim(args);if(out.text.empty()){out.errorKey="im_no_message";return out;}out.targets.push_back(*target);
 }else if(hook=="instantMessageTtell"){
  if(!targetName||targetName->empty()){out.errorKey="im_ttell_no_target";return out;}
  out.text=trim(args);if(out.text.empty()){out.errorKey="im_no_message";return out;}
  auto first=targetName->find_first_not_of(u" \n\r\t");if(first==targetName->npos){out.errorKey="im_ttell_no_target";return out;}
  auto last=targetName->find_first_of(u" \n\r\t",first);auto parsed=avatar(targetName->substr(first,last==targetName->npos?targetName->size()-first:last-first),context.self);
  if(!parsed){out.errorKey="im_ttell_no_target";return out;}out.targets.push_back(*parsed);
 }else {
  auto begin=args.find_first_not_of(u" \n\r\t");if(begin==args.npos){out.errorKey="im_tell_usage";return out;}
  auto finish=args.find_first_of(u" \n\r\t",begin);auto names=args.substr(begin,finish==args.npos?args.size()-begin:finish-begin);
  std::set<std::tuple<std::string,std::string,std::string>> sent;size_t at=0,count=0;
  while(at<names.size()){auto comma=names.find(u',',at);if(comma==names.npos)comma=names.size();auto token=names.substr(at,comma-at);at=comma+1;if(token.empty())continue;
   auto parsed=avatar(token,context.self);if(!parsed){out.errorKey="im_bad_target_format";return out;}++count;
   auto key=std::tuple(parsed->game,parsed->galaxy,parsed->name);if(!sent.insert(key).second)out.duplicates.push_back(*parsed);else out.targets.push_back(*parsed);
  }
  out.multiple=count>1;if(out.targets.empty()){out.errorKey="im_no_targets";return out;}
  if(finish==args.npos){out.errorKey="im_no_message";return out;}out.text=args.substr(finish+1);if(out.text.empty()){out.errorKey="im_no_message";return out;}
 }
 if(out.targets.size()>4){out.nativeError=u"Use at most four tell recipients per message.";return out;}
 // Core3 ignores remote tuple fields; never silently send to a local namesake.
 for(const auto& id:out.targets)if(clientcommon::lowerTell(id.game)!=clientcommon::lowerTell(context.self.game)||clientcommon::lowerTell(id.galaxy)!=clientcommon::lowerTell(context.self.galaxy)){out.nativeError=u"Tells to another game or galaxy are unavailable on this server.";return out;}
 return out;
}
}
