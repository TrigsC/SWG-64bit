#include "swgproto/StringIdChatParameter.h"
#include <stdexcept>
namespace swgproto {
std::u16string readBoundedChatUnicode(soe::PacketBuffer& b) {
    const auto count=b.readUint32();
    if(count>b.remaining()/2) throw std::out_of_range("Truncated chat Unicode");
    if(count>kMaxChatUnicodeUnits) throw std::runtime_error("Chat Unicode bound exceeded");
    std::u16string out;out.reserve(count);
    for(uint32_t i=0;i<count;++i)out.push_back(char16_t(b.readUint16()));
    return out;
}
namespace {
bool token(const std::string& value,bool key) {
    if(value.empty()||value.size()>128)return false;
    for(unsigned char c:value)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||(key&&c=='.')))return false;
    return value!="."&&value!="..";
}
std::string ascii(soe::PacketBuffer& b) {
    const auto count=b.readUint16();
    if(count>b.remaining())throw std::out_of_range("Truncated chat ASCII");
    if(count>1024)throw std::runtime_error("Chat ASCII bound exceeded");
    auto bytes=b.readBytes(count);return {bytes.begin(),bytes.end()};
}
StringId id(soe::PacketBuffer& b) {
    StringId out;out.file=ascii(b);
    if(b.readUint32()!=0)throw std::runtime_error("Nonzero StringId reserved field");
    out.stringId=ascii(b);return out;
}
StringIdChatParticipant participant(soe::PacketBuffer& b) {
    StringIdChatParticipant out;out.objectId=b.readUint64();out.id=id(b);out.text=readBoundedChatUnicode(b);return out;
}
}
bool isValidStringIdReference(const StringId& value) {
    if(value.numericId||value.file.empty()||value.file.size()>160||!token(value.stringId,true))return false;
    size_t begin=0;
    while(begin<value.file.size()){
        const auto slash=value.file.find('/',begin);
        const auto end=slash==std::string::npos?value.file.size():slash;
        if(!token(value.file.substr(begin,end-begin),false))return false;
        if(slash==std::string::npos)return true;
        begin=slash+1;
    }
    return false;
}
std::optional<StringId> parseStringIdReference(const std::u16string& text) {
    if(text.size()<4||text.front()!=u'@'||text.size()>290)return {};
    const auto colon=text.find(u':');
    if(colon==std::u16string::npos||text.find(u':',colon+1)!=std::u16string::npos)return {};
    StringId out;
    for(size_t i=1;i<text.size();++i){if(i==colon)continue;if(text[i]>127)return {}; (i<colon?out.file:out.stringId).push_back(char(text[i]));}
    if(!isValidStringIdReference(out))return {};return out;
}
std::optional<StringIdChatParameter> parseStringIdChatParameter(soe::PacketBuffer& b,size_t byteCount) {
    if(byteCount>b.remaining())throw std::out_of_range("Truncated chat parameter extent");
    if(byteCount>kMaxChatParameterBytes)throw std::runtime_error("Chat parameter bound exceeded");
    if(byteCount%2)throw std::runtime_error("Odd chat parameter extent");
    const auto bytes=b.readBytes(byteCount);soe::PacketBuffer p(bytes.data(),bytes.size());
    // Short unrecognized payloads remain unsupported, matching the prior guard.
    if(p.remaining()<7)return {};
    const auto pad=p.readUint16();const auto type=p.readByte();const auto marker=p.readUint32();
    if(type!=1||marker!=0xffffffff)return {};
    if(pad>1)throw std::runtime_error("Invalid StringId padding flag");
    StringIdChatParameter out;out.id=id(p);out.tu=participant(p);out.tt=participant(p);out.to=participant(p);
    out.di=p.readUint32();out.df=p.readFloat();out.unknownByte=p.readByte();
    if(p.remaining()!=pad)throw std::runtime_error("Invalid StringId completion/padding extent");
    if(pad&&p.readByte()!=0)throw std::runtime_error("Nonzero StringId padding");
    return out;
}
} // namespace swgproto
