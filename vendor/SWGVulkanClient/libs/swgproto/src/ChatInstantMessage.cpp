#include "swgproto/ChatInstantMessage.h"
#include "swgproto/StringIdChatParameter.h"
#include <stdexcept>
namespace swgproto {
namespace {
std::string ascii(soe::PacketBuffer& b){auto n=b.readUint16();if(n>1024||n>b.remaining())throw std::runtime_error("Invalid tell identity extent");auto bytes=b.readBytes(n);return {bytes.begin(),bytes.end()};}
void bounded(const ChatAvatarId& id){if(id.game.size()>1024||id.galaxy.size()>1024||id.name.empty()||id.name.size()>1024)throw std::invalid_argument("Invalid tell identity");}
}
ChatInstantMessage ChatInstantMessage::parse(soe::PacketBuffer& b){
 ChatInstantMessage out;out.sender={ascii(b),ascii(b),ascii(b)};out.text=readBoundedChatUnicode(b);out.outOfBand=readBoundedChatUnicode(b);
 if(b.remaining())throw std::runtime_error("Trailing instant message bytes");return out;
}
ChatInstantMessageResult ChatInstantMessageResult::parse(soe::PacketBuffer& b){
 ChatInstantMessageResult out{b.readUint32(),b.readUint32()};if(b.remaining())throw std::runtime_error("Trailing instant message acknowledgement");return out;
}
std::vector<uint8_t> buildChatInstantMessage(const ChatAvatarId& id,const std::u16string& text,uint32_t sequence){
 bounded(id);if(text.empty()||text.size()>512)throw std::invalid_argument("Invalid tell body extent");
 soe::PacketBuffer b;b.writeUint16(5);b.writeUint32(kChatInstantMessageToCharacterHash);b.writeAscii(id.game);b.writeAscii(id.galaxy);b.writeAscii(id.name);b.writeUnicode(text);b.writeUnicode(u"");b.writeUint32(sequence);return {b.data(),b.data()+b.size()};
}
}
