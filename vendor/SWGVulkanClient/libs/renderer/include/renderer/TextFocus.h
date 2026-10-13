#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <optional>

namespace renderer {
// Draft editing and bounded submission requests. Packet construction and
// network admission remain with the application owner.
// The same gate covers event taps, polled keys and mouse/camera input.
class TextFocus {
public:
    static constexpr size_t maxUnits=512;
    enum EditKey {Left=0x10025,Right=0x10027,Home=0x10024,End=0x10023,Delete=0x1002e};
    void enable(bool value){enabled_=value;if(!value)blur();}
    void beginFrame(){blockedFrame_=false;}
    bool active() const{return active_;}
    void enableSubmission(bool value){submissionEnabled_=value;if(!value)pending_.reset();}
    bool submissionEnabled() const{return submissionEnabled_;}
    std::optional<std::u16string> takeSubmission(){if(!pending_||taken_)return {};taken_=true;return pending_;}
    void finishSubmission(bool accepted){if(pending_&&accepted&&draft_==*pending_){draft_.clear();cursor_=0;blur();}pending_.reset();taken_=false;}
    bool blocked() const{return active_||blockedFrame_;}
    bool blocksKey(int key) const{return blocked()||quarantine_.contains(key);}
    const std::u16string& draft() const{return draft_;}
    size_t cursor() const{return cursor_;}
    bool selected() const{return selected_;}
    void focus(){if(!enabled_)return;active_=true;blockedFrame_=true;}
    void blur(){if(active_){quarantine_.insert(held_.begin(),held_.end());blockedFrame_=true;}active_=false;selected_=false;}
    void loseWindowFocus(){pending_.reset();taken_=false;blur();held_.clear();quarantine_.clear();}
    void blockUntilRelease(int key){quarantine_.insert(key);blockedFrame_=true;}
    void keyUp(int key){held_.erase(key);quarantine_.erase(key);}
    // true means consumed by the UI; callers must not enqueue world taps.
    bool keyDown(int key,bool repeat=false,bool shortcut=false) {
        held_.insert(key);
        if(!active_){if(enabled_&&key==13&&!repeat){focus();return true;}if(blockedFrame_)quarantine_.insert(key);return blocksKey(key);}
        if(key==27){pending_.reset();taken_=false;blur();return true;}
        if(key==13&&submissionEnabled_&&!repeat&&!pending_){pending_=draft_;taken_=false;return true;}
        if(shortcut&&key=='A'){selected_=true;cursor_=draft_.size();return true;}
        if(key==8) {
            if(selected_)eraseSelection();else if(cursor_){auto start=previous(cursor_);draft_.erase(start,cursor_-start);cursor_=start;}
        }else if(key==Delete){if(selected_)eraseSelection();else if(cursor_<draft_.size())draft_.erase(cursor_,next(cursor_)-cursor_);}
        else if(key==Left){cursor_=selected_?0:previous(cursor_);selected_=false;}
        else if(key==Right){cursor_=selected_?draft_.size():next(cursor_);selected_=false;}
        else if(key==Home){cursor_=0;selected_=false;}
        else if(key==End){cursor_=draft_.size();selected_=false;}
        return true; // Enter, shortcuts and all other keys remain UI-owned
    }
    void insert(std::u16string_view text) {
        if(!active_)return;
        if(selected_)eraseSelection();
        for(size_t i=0;i<text.size();) {
            auto c=text[i++];if(c<32||c==127)continue;
            std::u16string unit(1,c);
            if(c>=0xd800&&c<=0xdbff) {
                if(i<text.size()&&text[i]>=0xdc00&&text[i]<=0xdfff)unit+=text[i++];else unit[0]=0xfffd;
            }else if(c>=0xdc00&&c<=0xdfff)unit[0]=0xfffd;
            if(draft_.size()+unit.size()>maxUnits)break;
            draft_.insert(cursor_,unit);cursor_+=unit.size();
        }
    }
    void insertUtf8(std::string_view text) {
        std::u16string out;
        for(size_t i=0;i<text.size()&&out.size()<maxUnits;) {
            uint32_t code=uint8_t(text[i++]);int count=0;uint32_t minimum=0;
            if(code>=0xf0&&code<=0xf4){code&=7;count=3;minimum=0x10000;}
            else if(code>=0xe0&&code<=0xef){code&=15;count=2;minimum=0x800;}
            else if(code>=0xc2&&code<=0xdf){code&=31;count=1;minimum=0x80;}
            else if(code>=0x80){out+=char16_t(0xfffd);continue;}
            bool valid=true;
            for(int n=0;n<count;++n){if(i==text.size()||(uint8_t(text[i])&0xc0)!=0x80){valid=false;break;}code=(code<<6)|(uint8_t(text[i++])&63);}
            if(!valid||code<minimum||code>0x10ffff||(code>=0xd800&&code<=0xdfff))code=0xfffd;
            if(code>=0x10000){code-=0x10000;out+=char16_t(0xd800+(code>>10));out+=char16_t(0xdc00+(code&1023));}else out+=char16_t(code);
        }
        insert(out);
    }
private:
    size_t previous(size_t i) const{if(!i)return 0;--i;if(i&&draft_[i]>=0xdc00&&draft_[i]<=0xdfff&&draft_[i-1]>=0xd800&&draft_[i-1]<=0xdbff)--i;return i;}
    size_t next(size_t i) const{if(i>=draft_.size())return draft_.size();auto c=draft_[i++];if(c>=0xd800&&c<=0xdbff&&i<draft_.size()&&draft_[i]>=0xdc00&&draft_[i]<=0xdfff)++i;return i;}
    void eraseSelection(){draft_.clear();cursor_=0;selected_=false;}
    bool enabled_=false,active_=false,blockedFrame_=false,selected_=false;
    std::u16string draft_;size_t cursor_=0;
    bool submissionEnabled_=false,taken_=false;std::optional<std::u16string> pending_;
    std::unordered_set<int> held_,quarantine_;
};
}
