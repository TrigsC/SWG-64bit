#include "CommandCatalog.h"
#include "assets/IffReader.h"
#include "soe/PacketBuffer.h"
#include <set>
#include <variant>
#include <stdexcept>
#include <cmath>
#include <bit>

namespace dummyclient {
namespace {
using Cell=std::variant<std::string,int32_t,float>;
using Row=std::map<std::string,Cell>;
std::string lower(std::string value){for(char& c:value)if(c>='A'&&c<='Z')c+='a'-'A';return value;}
std::u16string unicode(const std::string& text){return {text.begin(),text.end()};}
std::string nul(soe::PacketBuffer& b){std::string out;while(true){auto c=b.readByte();if(!c)return out;if(out.size()==8192)throw std::runtime_error("Command resource string exceeds bound");out+=char(c);}}
void strictIff(const std::vector<uint8_t>& bytes,size_t start,size_t end,unsigned depth,size_t& nodes){
    if(depth>8)throw std::runtime_error("Command IFF depth bound");
    auto be=[&](size_t at){return uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3];};
    for(size_t at=start;at<end;){
        if(end-at<8||++nodes>10000)throw std::runtime_error("Incomplete command IFF extent");
        auto tag=be(at),size=be(at+4);if(size>end-at-8)throw std::runtime_error("Truncated command IFF extent");
        if(tag==0x464f524d){if(size<4)throw std::runtime_error("Incomplete command FORM");strictIff(bytes,at+12,at+8+size,depth+1,nodes);}
        at+=8+size;
    }
}
void strictIff(const std::vector<uint8_t>& bytes){if(bytes.size()>8*1024*1024)throw std::runtime_error("Command resource bound");size_t nodes=0;strictIff(bytes,0,bytes.size(),0,nodes);}
std::vector<Row> table(const std::vector<uint8_t>& bytes){
    if(bytes.size()>8*1024*1024)throw std::runtime_error("Command resource exceeds bound");
    strictIff(bytes);auto roots=assets::IffReader::parse(bytes);
    if(roots.size()!=1||roots[0].formType!=0x44544949||roots[0].children.size()!=1)throw std::runtime_error("Invalid DTII root");
    auto& version=roots[0].children[0];if(version.formType!=0x30303031||version.children.size()!=3)throw std::runtime_error("Unsupported DTII version");
    auto cols=version.findChild(0x434f4c53),types=version.findChild(0x54595045),rows=version.findChild(0x524f5753);
    if(!cols||!types||!rows)throw std::runtime_error("Missing DTII chunks");
    auto cb=cols->data,tb=types->data,rb=rows->data;auto count=cb.readUint32();if(!count||count>256)throw std::runtime_error("DTII column bound");
    std::vector<std::string> names,kinds;std::set<std::string> unique;
    for(uint32_t i=0;i<count;++i){auto name=nul(cb);if(!unique.insert(name).second)throw std::runtime_error("Duplicate DTII column");names.push_back(std::move(name));kinds.push_back(nul(tb));}
    if(cb.remaining()||tb.remaining())throw std::runtime_error("Trailing DTII columns");
    auto n=rb.readUint32();if(n>8192)throw std::runtime_error("DTII row bound");std::vector<Row> out;
    for(uint32_t i=0;i<n;++i){Row row;for(size_t j=0;j<count;++j){
        if(kinds[j].empty())throw std::runtime_error("Empty DTII type");auto kind=kinds[j][0];
        if(kind=='s')row[names[j]]=nul(rb);
        else if(kind=='f'){auto value=rb.readFloat();if(!std::isfinite(value))throw std::runtime_error("Invalid DTII float");row[names[j]]=value;}
        else if(std::string("ihbcpezI").find(kind)!=std::string::npos)row[names[j]]=std::bit_cast<int32_t>(rb.readUint32());
        else throw std::runtime_error("Unsupported DTII type");
    }out.push_back(std::move(row));}
    if(rb.remaining())throw std::runtime_error("Trailing DTII rows");return out;
}
std::vector<uint8_t> required(const HudAssetReader& read,const std::string& path){auto b=read(path);if(!b)throw std::runtime_error("Missing command resource: "+path);return std::move(*b);}
void replace(std::u16string& text,const std::u16string& token,const std::u16string& value){size_t at=0;while((at=text.find(token,at))!=std::u16string::npos){text.replace(at,token.size(),value);at+=value.size();}}
}
CommandCatalog CommandCatalog::load(const HudAssetReader& read){
    CommandCatalog out;
    auto names=decodeOriginalStringTable(required(read,"string/en/cmd_n.stf"));
    for(const auto& [file,client]:std::vector<std::pair<std::string,bool>>{{"command_table",false},{"command_table_ground",false},{"client_command_table",true},{"client_command_table_ground",true}}){
        for(const auto& row:table(required(read,"datatables/command/"+file+".iff"))){
            CommandDefinition d;d.name=lower(std::get<std::string>(row.at("commandName")));d.clientHook=std::get<std::string>(row.at("cppHook"));d.clientOwned=client;
            d.disabled=std::get<int32_t>(row.at("disabled"))!=0;d.targetType=std::get<int32_t>(row.at("targetType"));
            if(d.name.empty()||d.name.size()>64)throw std::runtime_error("Invalid command name");
            if(auto n=names.find(d.name);n!=names.end())d.label=n->second;else d.label=unicode(d.name);
            out.labels_[soe::MessageHash::compute(d.name)]=d.label;out.definitions_[d.name]=std::move(d);
        }
    }
    out.errors_=decodeOriginalStringTable(required(read,"string/en/cmd_err.stf"));
    out.locomotionNames_=decodeOriginalStringTable(required(read,"string/en/locomotion_n.stf"));out.stateNames_=decodeOriginalStringTable(required(read,"string/en/state_n.stf"));
    for(const auto& row:table(required(read,"datatables/include/locomotion.iff")))out.locomotions_.emplace(std::get<int32_t>(row.at("value")),lower(std::get<std::string>(row.at("locomotion"))));
    for(const auto& row:table(required(read,"datatables/include/state.iff")))out.states_.emplace(std::get<int32_t>(row.at("value")),lower(std::get<std::string>(row.at("state"))));
    auto spatialBytes=required(read,"chat/spatial_chat_types.iff");strictIff(spatialBytes);auto spatial=assets::IffReader::parse(spatialBytes);
    if(spatial.size()!=1||spatial[0].formType!=0x53504354||spatial[0].children.empty()||spatial[0].children[0].formType!=0x30303030)throw std::runtime_error("Unsupported spatial type resource");
    auto types=spatial[0].children[0].findChild(0x54595053);if(!types)throw std::runtime_error("Missing spatial types");auto tb=types->data;uint16_t index=0;
    while(tb.remaining()){if(++index>1024)throw std::runtime_error("Spatial type bound");if(nul(tb)=="say")out.sayType_=index;}
    if(!out.sayType_)throw std::runtime_error("Missing original say type");
    auto speech=decodeOriginalStringTable(required(read,"string/en/chat_types.stf"));auto say=speech.find("say");
    if(say==speech.end())throw std::runtime_error("Missing original say verb");auto newline=say->second.find(u'\n');
    if(newline==std::u16string::npos)throw std::runtime_error("Missing original third-person say verb");out.sayVerb_=say->second.substr(newline+1);return out;
}
ParsedCommand CommandCatalog::parse(const std::u16string& text,const ChatCommandContext& context) const{
    ParsedCommand out;size_t begin=text.find_first_not_of(u" ");if(begin==std::u16string::npos){out.empty=true;return out;}
    if(text.size()>512){out.error=u"The command is too long. It was not sent.";return out;}
    for(size_t i=0;i<text.size();++i){auto c=text[i];if(c<32||c==127){out.error=u"The command contains unsupported control characters.";return out;}
        if(c>=0xd800&&c<=0xdbff){if(++i>=text.size()||text[i]<0xdc00||text[i]>0xdfff){out.error=u"The command contains invalid text.";return out;}}
        else if(c>=0xdc00&&c<=0xdfff){out.error=u"The command contains invalid text.";return out;}}
    if(text[begin]!=u'/'){out.name="spatialchatinternal";out.arguments=text;out.spatial=true;}
    else {
        size_t end=text.find(u' ',begin+1);if(end==std::u16string::npos)end=text.size();
        auto token=text.substr(begin+1,end-begin-1);if(token.empty()||token.size()>64){out.error=u"Unknown command.";return out;}
        for(auto c:token){if(c>127||!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_')){out.error=u"Unknown command.";return out;}out.name+=char(c);}
        out.name=lower(out.name);size_t args=text.find_first_not_of(u" ",end);if(args!=std::u16string::npos)out.arguments=text.substr(args);
        if(out.name=="say"||out.name=="s"){out.name="spatialchatinternal";out.spatial=true;}
    }
    auto command=definitions_.find(out.name);
    if(command==definitions_.end()||command->second.disabled){out.error=u"Unknown or unavailable command.";return out;}
    // These posture commands have direct, source-confirmed server handlers.
    // Other client CPP hooks must be implemented, never blindly server-forwarded.
    if(command->second.clientOwned&&out.name!="stand"&&out.name!="kneel"){
        out.error=u"This command needs a client feature that is not available yet.";return out;}
    if(out.name=="spatialchatinternal"&&!out.spatial){out.error=u"Use ordinary chat text or /say to speak.";return out;}
    if(out.name=="spatialchat"){out.error=u"Use ordinary chat text or /say to speak.";return out;}
    if(out.spatial){
        if(out.arguments.empty()){out.empty=true;return out;}
        if(out.arguments.size()>255){out.error=u"Your message was "+unicode(std::to_string(out.arguments.size()))+u" characters long, which exceeds the 255 limit. Message was not sent.";return out;}
        if(context.language<0||context.language>255){out.error=u"Your current chat language is unavailable.";return out;}
        out.arguments=unicode(std::to_string(context.target))+u" "+unicode(std::to_string(sayType_))+u" "+unicode(std::to_string(context.mood))+u" 0 "+unicode(std::to_string(context.language))+u" "+out.arguments;
    }else out.target=context.target;
    return out;
}
std::optional<std::u16string> CommandCatalog::errorText(const clientcommon::CommandEvent& event) const{
    if(event.kind==clientcommon::CommandEvent::Kind::TransportRejected)return u"The command was not sent. Your draft has been kept.";
    if(event.kind!=clientcommon::CommandEvent::Kind::QueueCleared||event.error==0)return {};
    std::string key;std::u16string state;
    if(event.error==2)key="ability_prose";
    else if(event.error==3)key="target_type_prose";
    else if(event.error==4)key="target_range_prose";
    else if(event.error==1||event.error==5){
        const auto& indices=event.error==1?locomotions_:states_;const auto& names=event.error==1?locomotionNames_:stateNames_;
        if(auto index=indices.find(int(event.detail));index!=indices.end())if(auto value=names.find(index->second);value!=names.end())state=value->second;
        if(state.empty())return u"The command was rejected in your current state.";
        key=event.error==1?"locomotion_prose":"state_prose";
    }else return u"The command was rejected.";
    auto format=errors_.find(key);auto label=labels_.find(event.ticket.crc);if(format==errors_.end()||label==labels_.end())return u"The command was rejected.";
    auto result=format->second;replace(result,u"%TO",label->second);replace(result,u"%TU",state);
    if(result.find(u'%')!=std::u16string::npos)return u"The command was rejected.";return result;
}
}
