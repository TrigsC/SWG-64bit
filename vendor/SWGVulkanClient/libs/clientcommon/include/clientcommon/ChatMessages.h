#pragma once
#include "ChatLog.h"
#include "swgproto/ChatSystemMessage.h"
#include "swgproto/SpatialChat.h"
#include "TellSubmission.h"
#include "TellPresentation.h"
#include <stdexcept>
namespace clientcommon {
inline bool retainInstantChat(ChatLog& log,TellSubmission& submission,soe::PacketBuffer& buf){
 auto msg=swgproto::ChatInstantMessage::parse(buf);
 if(!msg.outOfBand.empty()||msg.sender.name.empty()||msg.text.empty()||msg.text.size()+11>ChatLog::maxCharacters){log.unsupported();return false;}
 submission.received(msg.sender);log.append(tellLine("im_received_prose",msg.sender,tellIncomingColor(msg.text),submission.context().self));return true;
}
inline bool retainSystemChat(ChatLog& log,soe::PacketBuffer& buf,ChatLine* observed=nullptr) {
    auto msg=swgproto::ChatSystemMessage::parse(buf);
    if(buf.remaining())throw std::runtime_error("Invalid system chat completion");
    if((msg.paramsSize&&!msg.localized)||(msg.displayType&~uint8_t(6))){
        log.unsupported();return false;
    }
    ChatLine line;line.text=std::move(msg.message);line.localized=std::move(msg.localized);line.displayType=msg.displayType;
    if(!line.localized&&line.text.starts_with(u"@"))if(auto id=swgproto::parseStringIdReference(line.text)){
        line.localized.emplace();line.localized->id=std::move(*id);line.text.clear();
    }
    if(observed)*observed=line;log.append(std::move(line));return true;
}
inline bool retainSpatialChat(ChatLog& log,soe::PacketBuffer& buf,ChatLine* observed=nullptr) {
    auto msg=swgproto::SpatialChat::parse(buf);
    if(buf.remaining()||msg.unknownTrailingLong||msg.unknownTrailingInt1||msg.unknownTrailingInt2||(msg.parameterized&&!msg.localized)){
        buf.readBytes(buf.remaining());log.unsupported();return false;
    }
    if(!msg.localized&&msg.message.starts_with(u"@")){
        auto id=swgproto::parseStringIdReference(msg.message);
        if(!id){log.unsupported();return false;}
        msg.localized.emplace();msg.localized->id=std::move(*id);msg.message.clear();
    }
    ChatLine line;line.kind=ChatLine::Kind::Spatial;line.sender=msg.senderId;line.target=msg.chatTargetId;
    line.text=std::move(msg.message);line.localized=std::move(msg.localized);line.chatType=msg.spatialChatType;line.mood=msg.moodType;line.language=msg.languageId;line.flags=msg.chatFlags;
    if(observed)*observed=line;log.append(std::move(line));return true;
}
}
