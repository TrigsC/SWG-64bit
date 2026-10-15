#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "LocalizedChat.h"
#include "clientcommon/ChatPresentation.h"
#include <limits>

using namespace dummyclient;

namespace {
void u32(std::vector<uint8_t>& bytes,uint32_t value){
    for(int i=0;i<4;++i)bytes.push_back(uint8_t(value>>(i*8)));
}
std::vector<uint8_t> stf(const std::vector<std::pair<std::string,std::u16string>>& entries){
    std::vector<uint8_t> bytes;u32(bytes,0xabcd);bytes.push_back(1);u32(bytes,0);u32(bytes,uint32_t(entries.size()));
    for(size_t i=0;i<entries.size();++i){
        u32(bytes,uint32_t(i+1));u32(bytes,0);u32(bytes,uint32_t(entries[i].second.size()));
        for(char16_t c:entries[i].second){bytes.push_back(uint8_t(c));bytes.push_back(uint8_t(c>>8));}
    }
    for(size_t i=0;i<entries.size();++i){
        u32(bytes,uint32_t(i+1));u32(bytes,uint32_t(entries[i].first.size()));
        bytes.insert(bytes.end(),entries[i].first.begin(),entries[i].first.end());
    }
    return bytes;
}
std::vector<uint8_t> syntheticTable(){
    return stf({
        {"first",u"Your lap took %DF seconds."},
        {"third",u"%TU finished a lap in %DF seconds."},
        {"unknown",u"Unknown %RT."},
        {"overlong",std::u16string(8189,u'x')+u"%DF"}
    });
}
LocalizedChat::ObjectContext noContext=[](uint64_t)->std::optional<LocalizedChat::ObjectInfo>{return {};};
LocalizedChat::ObjectContext named=[](uint64_t id)->std::optional<LocalizedChat::ObjectInfo>{
    if(id!=42)return {};return LocalizedChat::ObjectInfo{u"Pilot"};
};
}

TEST_CASE("finite float token resolves synthetic first and third person flat forms"){
    auto table=syntheticTable();size_t reads=0;
    LocalizedChat chat([&](const std::string& path)->std::optional<std::vector<uint8_t>>{
        ++reads;if(path=="string/en/synthetic.stf")return table;return {};
    });
    swgproto::StringIdChatParameter p;p.id={"synthetic","first"};p.df=12.5f;
    CHECK(chat.formatWithContext(p,noContext)==u"Your lap took 12.5 seconds.");
    p.id.stringId="third";p.tu.objectId=42;
    CHECK(chat.formatWithContext(p,named)==u"Pilot finished a lap in 12.5 seconds.");
    CHECK_FALSE(chat.formatWithContext(p,noContext));
    CHECK(reads==1);
}

TEST_CASE("unavailable participants and unsupported forms stay visible then resolve when context arrives"){
    auto table=syntheticTable();
    LocalizedChat chat([&](const std::string& path)->std::optional<std::vector<uint8_t>>{
        if(path=="string/en/synthetic.stf")return table;return {};
    });
    swgproto::StringIdChatParameter p;p.id={"synthetic","third"};p.tu.objectId=42;p.df=1.25f;
    clientcommon::ChatSnapshot display,raw;raw.revision=9;clientcommon::ChatLine line;line.sequence=9;line.localized=p;raw.lines.push_back(line);
    auto missing=chat.presentRetainedWithContext(9,p,noContext);
    CHECK_FALSE(missing.resolved);CHECK(missing.text==u"A localized message is unavailable.");
    raw.lines[0].text=missing.text;CHECK(clientcommon::updateChatPresentation(display,raw));
    REQUIRE(display.lines.size()==1);CHECK(display.lines[0].sequence==9);
    auto ready=chat.presentRetainedWithContext(9,p,named);
    CHECK(ready.resolved);CHECK(ready.text==u"Pilot finished a lap in 1.25 seconds.");
    raw.lines[0].text=ready.text;CHECK(clientcommon::updateChatPresentation(display,raw));
    CHECK(display.lines[0].text==ready.text);
    CHECK(chat.presentRetainedWithContext(9,p,noContext).text==ready.text);
}

TEST_CASE("nonfinite malformed unknown and oversized forms use a bounded fallback"){
    auto table=syntheticTable();size_t reads=0;
    LocalizedChat chat([&](const std::string& path)->std::optional<std::vector<uint8_t>>{
        ++reads;if(path=="string/en/synthetic.stf")return table;return {};
    });
    swgproto::StringIdChatParameter p;p.id={"synthetic","first"};p.df=std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(chat.formatWithContext(p,noContext));
    CHECK_FALSE(chat.presentRetainedWithContext(1,p,noContext).resolved);
    p.df=std::numeric_limits<float>::infinity();
    CHECK_FALSE(chat.formatWithContext(p,noContext));
    p.df=12.5f;p.id.stringId="unknown";
    CHECK_FALSE(chat.formatWithContext(p,noContext));
    p.id.stringId="overlong";
    auto over=chat.presentRetainedWithContext(2,p,noContext);
    CHECK_FALSE(over.resolved);CHECK(over.text.size()<128);
    auto before=reads;p.id={"../escape","first"};
    CHECK_FALSE(chat.presentRetainedWithContext(3,p,noContext).resolved);CHECK(reads==before);
    p.id={"synthetic","first"};p.unknownByte=1;
    CHECK_FALSE(chat.presentRetainedWithContext(4,p,noContext).resolved);
}
