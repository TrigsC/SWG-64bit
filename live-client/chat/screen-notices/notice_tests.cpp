#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "clientcommon/ScreenNotices.h"
#include "clientcommon/ChatText.h"
#include "OriginalHudAssets.h"
#include <fstream>
#include <limits>
using namespace clientcommon;
TEST_CASE("original normal FIFO deadline belongs to oldest and does not restart on new arrival"){
 ScreenNotices q;q.admit(1,u"first",0);for(int i=0;i<8;++i)q.heartbeat();CHECK(q.rows()[0].opacity==1);
 q.admit(2,u"second",120);CHECK(q.deadline()==240);q.update(240);CHECK(q.rows()[0].enabled);q.update(241);CHECK_FALSE(q.rows()[0].enabled);CHECK(q.rows()[1].enabled);
 for(int i=0;i<60;++i)q.heartbeat();CHECK(q.rows()[0].opacity>0);q.update(301);CHECK(q.rows().size()==2);
 q.heartbeat();CHECK(q.rows()[0].opacity==0);q.update(302);REQUIRE(q.rows().size()==1);CHECK(q.rows()[0].sequence==2);CHECK(q.deadline()==542);
 q.update(542);CHECK(q.rows()[0].enabled);q.update(543);CHECK_FALSE(q.rows()[0].enabled);
}
TEST_CASE("overflow oldest inherits deadline duplicates remain and large rows can cull themselves"){
 ScreenNotices q;for(int i=0;i<6;++i){q.admit(i+1,u"duplicate",i*20);q.rows().back().height=21;q.rows().back().width=37;}
 q.packAndCull(491,101,4);REQUIRE(q.rows().size()==4);CHECK(q.rows().front().sequence==3);CHECK(q.deadline()==240);CHECK(q.rows().back().x==227);CHECK(q.rows().back().y==78);
 q.admit(7,u"long",200);q.rows().back().height=200;q.rows().back().width=491;q.packAndCull(491,101,4);CHECK(q.rows().empty());q.admit(8,u"new",210);CHECK(q.deadline()==450);
}
TEST_CASE("screen routing rejects chat-only and spatial but permits late-resolved system text exactly once"){
 ScreenNotices q;ChatSnapshot raw,shown;raw.revision=5;for(int i=1;i<=3;++i){ChatLine line;line.sequence=i;line.text=u"sample";if(i==2)line.displayType=2;if(i==3)line.kind=ChatLine::Kind::Spatial;raw.lines.push_back(line);}shown=raw;shown.lines.erase(shown.lines.begin());
 q.synchronize(raw,shown,0);CHECK(q.rows().empty());shown=raw;q.synchronize(raw,shown,60);REQUIRE(q.rows().size()==1);CHECK(q.rows()[0].sequence==1);CHECK(q.deadline()==300);
 q.synchronize(raw,shown,100);CHECK(q.rows().size()==1);CHECK(q.seenCount()==3);
 raw.lines.clear();shown.lines.clear();q.synchronize(raw,shown,200);CHECK(q.seenCount()==0); // Active overlay remains independent of chat eviction.
}
TEST_CASE("UIClock catch-up unrolls from ticks while fade advances once per notification"){
 ScreenNotices q;q.admit(1,u"A sufficiently long sample line with many characters",10);auto& row=q.rows()[0];
 CHECK(ScreenNotices::unrollLimit(row,10)==0);CHECK(ScreenNotices::textAlpha(row)==0);q.heartbeat();CHECK(row.opacity==doctest::Approx(8.f/60.f));
 CHECK(ScreenNotices::unrollLimit(row,16)==18);CHECK(row.opacity==doctest::Approx(8.f/60.f));CHECK(ScreenNotices::textAlpha(row)==doctest::Approx(.5511111f));
 CHECK(ScreenNotices::unrollLimit(row,100)==0);row.enabled=false;CHECK(ScreenNotices::textAlpha(row)==row.opacity);
 row.opacity=1;row.enabled=true;CHECK(ScreenNotices::textAlpha(row,.8,.75)==doctest::Approx(.95));row.enabled=false;CHECK(ScreenNotices::textAlpha(row,.8,.75)==doctest::Approx(.75));
}
TEST_CASE("deferred empty presentation remains eligible and zero opacity disables before removal"){
 ScreenNotices q;ChatSnapshot raw,shown;ChatLine line;line.sequence=0x100000001ULL;line.text=u"resolved";raw.lines.push_back(line);shown=raw;shown.lines[0].text.clear();
 q.synchronize(raw,shown,0);CHECK(q.seenCount()==0);CHECK(q.rows().empty());shown=raw;q.synchronize(raw,shown,20);REQUIRE(q.rows().size()==1);CHECK(q.rows()[0].sequence==line.sequence);
 q.synchronize(raw,shown,21);CHECK(q.rows().size()==1);q.update(261);REQUIRE(q.rows().size()==1);CHECK_FALSE(q.rows()[0].enabled);q.update(262);CHECK(q.rows().empty());
}
TEST_CASE("native admission is bounded and invalid timing policy cannot create nonfinite UI state"){
 ScreenNotices q;for(int i=0;i<1000;++i)q.admit(i+1,u"valid",i);CHECK(q.rows().size()==256);CHECK(q.rows().front().sequence==745);
 q.admit(1001,std::u16string(8193,u'x'),1000);CHECK(q.rows().size()==256);CHECK(q.rows().back().sequence==1000);
 CHECK_THROWS(ScreenNotices(0,1,1));CHECK_THROWS(ScreenNotices(8,1,0));CHECK_THROWS(ScreenNotices(8,1,std::numeric_limits<float>::infinity()));
 ScreenNotices longer(8,1,2);longer.admit(1,u"longer",0);CHECK(longer.deadline()==480);
}
TEST_CASE("notice widget resources retain original bold font packing margins palette and fades"){
 auto read=[](const std::string& p)->std::optional<std::vector<uint8_t>>{std::ifstream f(std::string(SWG_CHAT_ROOT)+"/"+p,std::ios::binary);if(!f)return {};return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),{});};
 auto a=dummyclient::OriginalScreenNoticeAssets::load(read);CHECK(a.page.x==267);CHECK(a.page.y==81);CHECK(a.page.width==491);CHECK(a.page.height==102);CHECK(a.composite.height==101);CHECK(a.spacing==4);CHECK(a.margin==std::array<int,4>{4,4,4,4});CHECK(a.textColor==std::array<float,4>{1,1,1,1});CHECK(a.fadeInSpeed==8);CHECK(a.fadeOutSpeed==1);CHECK(a.textMinimum==doctest::Approx(.8));CHECK(a.sizeIncrement==std::array<int,2>{1,25});CHECK(a.parentIdleOpacity==doctest::Approx(.75));CHECK(a.parentFadeSpeed==1);
}
TEST_CASE("source centered extents exclude wrapping spaces but preserve explicit newline spaces"){
 std::vector<int> widths;auto advance=[](uint32_t){return 1;};
 auto lines=wrapChatText(u"ab cd",3,advance,{1,1,1,1},{},&widths);REQUIRE(lines.size()==2);CHECK(lines[0].size()==3);CHECK(widths==std::vector<int>{2,2});
 lines=wrapChatText(u"ab \ncd ",100,advance,{1,1,1,1},{},&widths);REQUIRE(lines.size()==2);CHECK(widths==std::vector<int>{3,3});
 lines=wrapChatText(u"ab-\\#ff0000cd",3,advance,{1,1,1,1},{},&widths);REQUIRE(lines.size()==2);CHECK(widths==std::vector<int>{3,2});CHECK(lines[1][0].color[0]==1);CHECK(lines[1][0].color[1]==0);
}
