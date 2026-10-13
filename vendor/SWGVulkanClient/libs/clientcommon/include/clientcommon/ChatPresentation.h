#pragma once
#include "ChatLog.h"
#include <algorithm>
namespace clientcommon {
// Latest incoming text gets first admission to bounded resource work; retry
// passes rotate fairly over older deferred lines without changing display order.
class ChatResolveSchedule {
public:
    std::vector<size_t> order(const ChatSnapshot& raw){
        if(raw.revision!=revision_){cursor_=0;revision_=raw.revision;}
        const size_t count=raw.lines.size();std::vector<size_t> indices;indices.reserve(count);
        if(!count){cursor_=0;return indices;}
        cursor_%=count;for(size_t i=0;i<count;++i)indices.push_back(count-1-(cursor_+i)%count);
        cursor_=(cursor_+1)%count;return indices;
    }
private:
    uint64_t revision_=0;size_t cursor_=0;
};
// Network revision and visible text revision are distinct: a late object name
// may resolve without any new packet. Keep the HUD's text cache consistent.
inline bool updateChatPresentation(ChatSnapshot& displayed,ChatSnapshot next){
    std::erase_if(next.lines,[](const auto& line){return line.text.empty();});
    bool changed=next.lines.size()!=displayed.lines.size();
    if(!changed)for(size_t i=0;i<next.lines.size();++i){const auto& a=next.lines[i];const auto& b=displayed.lines[i];
        if(a.sequence!=b.sequence||a.kind!=b.kind||a.text!=b.text){changed=true;break;}
    }
    next.revision=displayed.revision+(changed?1:0);displayed=std::move(next);return changed;
}
}
