#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <clientcommon/ChatMessages.h>
#include <tuple>
#include "LocalizedChat.h"
#include <fstream>
#include <filesystem>
#include <clientcommon/ChatTabText.h>
using namespace clientcommon;
static ChatLine line(std::u16string text,ChatLine::Kind kind=ChatLine::Kind::System){ChatLine out;out.text=std::move(text);out.kind=kind;return out;}
static ChatSnapshot selected(ChatLog& log,size_t tab){log.selectTab(tab);ChatSnapshot out;log.snapshotActiveIfChanged(UINT64_MAX,out);return out;}
TEST_CASE("receive types and explicit acknowledgement broadcasts reach original separate buffers"){
 ChatLog log;log.append(line(u"nearby",ChatLine::Kind::Spatial));log.append(line(u"tell",ChatLine::Kind::Instant));log.append(line(u"system"));
 auto quest=line(u"quest");quest.displayType=4;log.append(quest);
 auto ack=line(u"ack",ChatLine::Kind::Instant);ack.delivery=ChatLine::Delivery::AllTabs;log.append(ack);
 REQUIRE(selected(log,0).lines.size()==5);REQUIRE(selected(log,1).lines.size()==3);REQUIRE(selected(log,2).lines.size()==1);REQUIRE(selected(log,3).lines.size()==2);
 CHECK(selected(log,2).lines.front().text==u"ack");CHECK(selected(log,3).lines.front().text==u"quest");
 auto feedback=line(u"local");feedback.delivery=ChatLine::Delivery::CurrentTab;log.append(feedback);CHECK(selected(log,3).lines.back().text==u"local");CHECK(selected(log,0).lines.size()==5);
}
TEST_CASE("quiet tab history survives unrelated global and active history eviction"){
 ChatLog log;auto planet=line(u"quiet planet");planet.channel=ChatChannelId::builtin(ChatChannelType::Planet);log.append(planet);
 for(size_t i=0;i<600;++i)log.append(line(u"nearby",ChatLine::Kind::Spatial));
 auto spatial=selected(log,0);CHECK(spatial.lines.size()==256);CHECK(spatial.evicted==344);
 auto quiet=selected(log,2);REQUIRE(quiet.lines.size()==1);CHECK(quiet.lines[0].text==u"quiet planet");CHECK(quiet.evicted==0);
 auto before=quiet.revision;CHECK_FALSE(log.selectTab(2));CHECK_FALSE(log.snapshotActiveIfChanged(before,quiet));CHECK_FALSE(log.selectTab(SIZE_MAX));CHECK(log.activeTab()==2);
 log.clear();CHECK(log.activeTab()==0);CHECK(selected(log,2).lines.empty());
}
TEST_CASE("original tab text strips one trailing LF and preserves existing separators"){
 for(const auto& [before,text,after]:std::vector<std::tuple<std::u16string,std::u16string,std::u16string>>{
  {u"",u"one\n",u"one"},{u"one",u"two",u"one\ntwo"},{u"one\n",u"two",u"one\ntwo"},
  {u"one",u"\ntwo\n\n",u"one\ntwo\n"},{u"one",u"\n",u"one"},{u"one",u"",u"one"}}){auto actual=before;appendTabText(actual,text);CHECK(actual==after);}
}
TEST_CASE("actual supported receiver populates histories and malformed payload cannot mutate them"){
 ChatLog log;soe::PacketBuffer data;data.writeByte(2);data.writeUnicode(u"system");data.writeUint32(0);data.resetReadCursor();REQUIRE(retainSystemChat(log,data));
 CHECK(selected(log,0).lines.size()==1);CHECK(selected(log,1).lines.size()==1);CHECK(selected(log,2).lines.empty());
 soe::PacketBuffer bad;bad.writeByte(1);bad.writeUnicode(u"bad");bad.writeUint32(0);bad.resetReadCursor();CHECK_FALSE(retainSystemChat(log,bad));CHECK(selected(log,0).lines.size()==1);
 for(uint8_t flags:{4,6}){soe::PacketBuffer quest;quest.writeByte(flags);quest.writeUnicode(u"quest");quest.writeUint32(0);quest.resetReadCursor();REQUIRE(retainSystemChat(log,quest));}
 CHECK(selected(log,3).lines.size()==2);CHECK(selected(log,1).lines.size()==1);
}
TEST_CASE("quiet localized history retains rendered participant after global eviction and despawn"){
 auto reader=[](const std::string& name)->std::optional<std::vector<uint8_t>>{std::ifstream f(std::filesystem::path(SWG_CHAT_ROOT)/name,std::ios::binary);if(!f)return {};return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),{});};
 dummyclient::LocalizedChat local(reader);ChatLog log;auto message=line(u"");message.localized.emplace();message.localized->id={"pvp_rating","pvp_rating_target"};message.localized->tt.objectId=0x100000001ull;message.localized->di=42;
 message.channel=ChatChannelId::builtin(ChatChannelType::Planet);log.append(message);auto quiet=selected(log,2);REQUIRE(quiet.lines.size()==1);
 auto known=[](uint64_t)->std::optional<std::u16string>{return u"Novi";};local.beginPass();auto original=local.formatRetained(quiet.lines[0].sequence,*quiet.lines[0].localized,known);REQUIRE(original);CHECK(*original==u"Novi has a player vs. player combat rating of 42.");
 message.channel=ChatChannelId::builtin(ChatChannelType::Spatial);
 for(int i=0;i<600;++i){log.append(message);ChatSnapshot raw;log.snapshotIfChanged(UINT64_MAX,raw);local.beginPass();REQUIRE(local.formatRetained(raw.lines.back().sequence,*raw.lines.back().localized,known));local.retainSequences(log.retainedSequences());}
 quiet=selected(log,2);REQUIRE(quiet.lines.size()==1);CHECK(local.resolvedLines()>256);local.retainSequences(log.retainedSequences());
 auto gone=[](uint64_t)->std::optional<std::u16string>{return {};};local.beginPass();CHECK(local.formatRetained(quiet.lines[0].sequence,*quiet.lines[0].localized,gone)==original);
 log.clear();local.retainSequences(log.retainedSequences());CHECK(local.resolvedLines()==0);
}
