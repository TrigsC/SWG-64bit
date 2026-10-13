#include "swgproto/SpatialChat.h"
#include <bit>
#include <stdexcept>
namespace swgproto {
SpatialChat SpatialChat::parse(soe::PacketBuffer& buf) {
    SpatialChat out;out.senderId=buf.readUint64();out.chatTargetId=buf.readUint64();out.message=readBoundedChatUnicode(buf);
    out.volume=buf.readUint16();out.spatialChatType=buf.readUint16();out.moodType=buf.readUint16();out.chatFlags=buf.readByte();out.languageId=buf.readByte();
    // Ordinary empty speech has a zero sixteen-byte tail. A nonzero first
    // word with the recognized StringId header identifies even a truncated
    // parameterized container, so it cannot silently become an ordinary tail.
    const auto at=buf.readPosition();
    const bool sidPrefix=buf.remaining()>=11&&buf.peekByte(at+6)==1&&buf.peekByte(at+7)==0xff&&buf.peekByte(at+8)==0xff&&buf.peekByte(at+9)==0xff&&buf.peekByte(at+10)==0xff;
    if(!out.message.empty()||(buf.remaining()==16&&!sidPrefix)) {
        out.unknownTrailingLong=buf.readUint64();out.unknownTrailingInt1=std::bit_cast<int32_t>(buf.readUint32());out.unknownTrailingInt2=std::bit_cast<int32_t>(buf.readUint32());
    } else {
        out.parameterized=true;out.paramsSize=buf.readUint32();
        if(buf.remaining()<4||out.paramsSize!=(buf.remaining()-4)/2||(buf.remaining()-4)%2||!out.paramsSize)throw std::runtime_error("Invalid spatial chat parameter extent");
        out.localized=parseStringIdChatParameter(buf,size_t(out.paramsSize)*2);
        if(buf.readUint32()!=0)throw std::runtime_error("Nonzero spatial parameter tail");
    }
    return out;
}
}
