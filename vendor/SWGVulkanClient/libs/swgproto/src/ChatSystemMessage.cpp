#include "swgproto/ChatSystemMessage.h"
#include <stdexcept>
namespace swgproto {
ChatSystemMessage ChatSystemMessage::parse(soe::PacketBuffer& buf) {
    ChatSystemMessage out;out.displayType=buf.readByte();out.message=readBoundedChatUnicode(buf);out.paramsSize=buf.readUint32();
    if(out.paramsSize) {
        if(out.paramsSize!=buf.remaining()/2||buf.remaining()%2)throw std::runtime_error("Invalid system chat parameter extent");
        if(!out.message.empty())throw std::runtime_error("Mixed literal and parameterized system chat");
        out.localized=parseStringIdChatParameter(buf,buf.remaining());
    }
    return out;
}
}
