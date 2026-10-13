#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "clientcommon/ChatMessages.h"
#include "clientcommon/ChatText.h"
#include "renderer/TextFocus.h"
#include "OriginalHudAssets.h"
#include <fstream>
#include <thread>
using namespace clientcommon;
namespace {
std::optional<std::vector<uint8_t>> file(const std::string& name){std::ifstream f(std::string(SWG_CHAT_ROOT)+"/"+name,std::ios::binary);if(!f)return {};return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),{});}
}
TEST_CASE("plain system wire stays literal and invalid counts cannot allocate huge strings") {
    ChatLog log;soe::PacketBuffer b;b.writeByte(2);b.writeUnicode(u"Hi!");b.writeUint32(0);CHECK(retainSystemChat(log,b));CHECK(b.remaining()==0);
    ChatSnapshot s;CHECK(log.snapshotIfChanged(0,s));REQUIRE(s.lines.size()==1);CHECK(s.lines[0].text==u"Hi!");CHECK(s.lines[0].displayType==2);
    CHECK_FALSE(log.snapshotIfChanged(s.revision,s));
    soe::PacketBuffer huge;huge.writeByte(0);huge.writeUint32(UINT32_MAX);huge.writeUint32(0);CHECK_THROWS_AS(retainSystemChat(log,huge),std::out_of_range);
    soe::PacketBuffer bad;bad.writeByte(0);bad.writeUnicode(u"");bad.writeUint32(1);bad.writeByte(0);CHECK_THROWS(retainSystemChat(log,bad));
    // word count1 means two payload bytes, not one; no localized string invented.
    soe::PacketBuffer words;words.writeByte(0);words.writeUnicode(u"");words.writeUint32(1);words.writeUint16(0);CHECK_FALSE(retainSystemChat(log,words));
    CHECK(log.snapshotIfChanged(s.revision,s));CHECK(s.unsupported==1);CHECK(s.lines.size()==1);
}
TEST_CASE("spatial Unicode metadata is preserved without guessed speaker grammar") {
    ChatLog log;soe::PacketBuffer b;
    b.writeUint64(0x100000001);b.writeUint64(0x100000002);b.writeUnicode(u"Hi");
    b.writeUint16(64);b.writeUint16(0);b.writeUint16(5);b.writeByte(3);b.writeByte(2);
    b.writeUint64(0);b.writeUint32(0);b.writeUint32(0);
    CHECK(retainSpatialChat(log,b));ChatSnapshot s;log.snapshotIfChanged(0,s);REQUIRE(s.lines.size()==1);
    CHECK(s.lines[0].sender==0x100000001);CHECK(s.lines[0].target==0x100000002);CHECK(s.lines[0].text==u"Hi");CHECK(s.lines[0].language==2);CHECK(s.lines[0].mood==5);
}
TEST_CASE("bounded history snapshots remain ordered under concurrent receive and render") {
    ChatLog log;std::thread receiver([&](){for(int i=0;i<1000;++i){ChatLine l;l.text=u"message";log.append(std::move(l));}});
    ChatSnapshot s;for(int i=0;i<100;++i)log.snapshotIfChanged(s.revision,s);receiver.join();log.snapshotIfChanged(s.revision,s);
    REQUIRE(s.lines.size()==256);CHECK(s.evicted==744);CHECK(s.lines.front().sequence==745);CHECK(s.lines.back().sequence==1000);
    ChatLine oversized;oversized.text.assign(8193,u'a');log.append(std::move(oversized));log.snapshotIfChanged(s.revision,s);CHECK(s.unsupported==1);CHECK(s.lines.size()==256);
}
TEST_CASE("draft focus consumes taps polling escapes and held-key releases") {
    renderer::TextFocus f;f.enable(true);f.beginFrame();CHECK_FALSE(f.keyDown('W'));CHECK(f.keyDown(13));CHECK(f.active());
    for(int key:std::array<int,14>{'W','A','S','D','R','E','C','M','P','I','Q',9,8,27}) {
        if(key==27)break;CHECK(f.keyDown(key));CHECK(f.blocksKey(key));
    }
    f.insertUtf8("wasd r e c m p i q");CHECK(f.draft()==u"wasd r e c m p i q");
    CHECK(f.keyDown(13));CHECK(f.active());CHECK(f.keyDown(27));CHECK_FALSE(f.active());CHECK(f.blocksKey(27));
    f.beginFrame();CHECK(f.blocksKey('W'));f.keyUp('W');CHECK_FALSE(f.blocksKey('W'));f.keyUp(27);CHECK_FALSE(f.blocksKey(27));
    f.blockUntilRelease(0x1000);f.beginFrame();CHECK(f.blocksKey(0x1000));f.keyUp(0x1000);CHECK_FALSE(f.blocksKey(0x1000));
    f.focus();f.loseWindowFocus();CHECK_FALSE(f.active());f.beginFrame();CHECK_FALSE(f.blocksKey('R'));
}
TEST_CASE("Unicode cursor deletion selection and size cap never split surrogate pairs") {
    renderer::TextFocus f;f.enable(true);f.focus();f.insertUtf8("A\xf0\x9f\x9a\x80" "B");CHECK(f.draft()==u"A\U0001f680B");
    f.keyDown(renderer::TextFocus::Left);f.keyDown(8);CHECK(f.draft()==u"AB");CHECK(f.cursor()==1);
    f.keyDown('A',false,true);f.insertUtf8("\xc3\xa9");CHECK(f.draft()==u"é");
    const auto before=f.cursor();for(int key:{37,39,36,35,46})f.keyDown(key);CHECK(f.cursor()==before);CHECK(f.draft()==u"é");
    f.insert(std::u16string(600,u'x'));CHECK(f.draft().size()==512);f.keyDown('A',false,true);f.insertUtf8("\xff\n\r");CHECK(f.draft()==u"\ufffd");
}
TEST_CASE("original chat fonts geometry and duplicated backgrounds are loaded without sample text") {
    auto a=dummyclient::OriginalChatAssets::load(file);CHECK(a.nodes[0].layout.width==523);CHECK(a.nodes[0].layout.height==145);
    CHECK(a.nodes[a.output].layout.width==484);CHECK(a.nodes[a.input].layout.width==424);
    CHECK(a.fonts.at("default_12").leading==12);CHECK(a.fonts.at("default_13").leading==13);
    CHECK(a.outputMargin==std::array<int,4>{2,2,2,2});CHECK(a.inputPadding==std::array<int,4>{8,4,2,2});CHECK(a.caretWidth==2);
    size_t backgrounds=0;for(const auto& n:a.nodes)if(n.path=="/chatwindow/collapsing/output/background")++backgrounds;CHECK(backgrounds==2);
    for(const auto& n:a.nodes)if(n.style)for(const auto& [_,s]:n.style->slices)CHECK(s.texturePath=="texture/ui_rebel_final.dds");
}
TEST_CASE("source-backed wrapping RGB reset and palette escapes use glyph advances") {
    auto a=dummyclient::OriginalChatAssets::load(file);const auto& font=a.fonts.at("default_12");
    auto wrap=[&](const std::u16string& text,int width){return wrapChatText(text,width,[&](uint32_t c){auto p=font.glyphs.find(c);return p==font.glyphs.end()?font.glyphs.at(0).advance:p->second.advance;},a.outputColor,a.palette);};
    CHECK(wrap(u"abc",480).size()==1);CHECK(wrap(u"a\n",480).size()==2);CHECK(wrap(u"a\r\nb",480).size()==2);
    CHECK(wrap(u"iiiiiiii",8).size()==4);auto words=wrap(u"alpha beta",32);REQUIRE(words.size()==2);CHECK(words[0].size()==6);CHECK(words[1].size()==4);
    auto colors=wrap(u"\\#ff0000R\\#.x\\#ptext1 y",480);REQUIRE(colors.size()==1);REQUIRE(colors[0].size()==3);
    CHECK(colors[0][0].color==std::array<float,4>{1,0,0,1});CHECK(colors[0][1].color==a.outputColor);CHECK(colors[0][2].color==a.outputColor);
}
