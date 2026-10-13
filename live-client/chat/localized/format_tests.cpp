#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "LocalizedChat.h"
#include "clientcommon/ChatPresentation.h"
#include <fstream>
#include <limits>
#include <set>
using namespace dummyclient;
namespace {
std::optional<std::vector<uint8_t>> resource(const std::string& p){std::ifstream f(std::string(SWG_CHAT_ROOT)+"/"+p,std::ios::binary);if(!f)return {};return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),{});}
std::optional<std::u16string> noName(uint64_t){return {};}
}
TEST_CASE("original plain references and signed integer prose use active original resources"){
 LocalizedChat chat(resource);swgproto::StringIdChatParameter p;p.id={"base_player","tip_syntax"};
 CHECK(chat.format(p,noName)==u"SYNTAX: /tip <amount> (to current target) or /tip <name> <amount>");
 p.id={"pvp_rating","pvp_rating"};p.di=0;CHECK(chat.format(p,noName)==u"Your player vs. player combat rating is 0.");
 p.di=0xffffffff;CHECK(chat.format(p,noName)==u"Your player vs. player combat rating is -1.");
 p.di=0x80000000;CHECK(chat.format(p,noName)==u"Your player vs. player combat rating is -2147483648.");
 p.di=0x7fffffff;CHECK(chat.format(p,noName)==u"Your player vs. player combat rating is 2147483647.");
 CHECK(chat.cachedTables()==2);CHECK(chat.tableLoads()==2);
}
TEST_CASE("literal names full 64-bit object names and nested references use explicit participant forms"){
 LocalizedChat chat(resource);swgproto::StringIdChatParameter p;p.id={"pvp_rating","pvp_rating_target"};p.di=123;p.tt.text=u"Novi";
 CHECK(chat.format(p,noName)==u"Novi has a player vs. player combat rating of 123.");
 p.tt.text.clear();p.tt.objectId=0x200000001;uint64_t observed=0;
 CHECK(chat.format(p,[&](uint64_t id)->std::optional<std::u16string>{observed=id;return u"Novi";})==u"Novi has a player vs. player combat rating of 123.");CHECK(observed==0x200000001);
 CHECK_FALSE(chat.format(p,noName));p.tt.objectId=0;p.tt.id={"base_player","tip_syntax"};
 CHECK(chat.format(p,noName)==u"SYNTAX: /tip <amount> (to current target) or /tip <name> <amount> has a player vs. player combat rating of 123.");
 p.tt.id={};p.tt.text=u"@pvp_rating:pvp_rating";
 CHECK(chat.format(p,noName)==u"Your player vs. player combat rating is %DI. has a player vs. player combat rating of 123.");
}
TEST_CASE("replacement text is not rescanned and unavailable objects use literal fallback before unsupported flags defer"){
 LocalizedChat chat(resource);swgproto::StringIdChatParameter p;p.id={"base_player","prose_tip_invalid_param"};p.to.text=u"%DI \U0001f680";p.di=99;
 CHECK(chat.format(p,noName)==u"/TIP: invalid amount (\"%DI \U0001f680\") parameter.");
 p.to.objectId=0x100000001;CHECK(chat.format(p,noName)==u"/TIP: invalid amount (\"%DI \U0001f680\") parameter.");p.to.objectId=0;p.unknownByte=1;CHECK_FALSE(chat.format(p,noName));
 p.unknownByte=0;p.to.text.clear();p.id={"base_player","prose_tip_nsf_cash"};p.tt.text=u"Novi";CHECK(chat.format(p,noName)==u"You lack the cash funds to tip 99 credits to Novi.");
 p.id={"no_such_native_table","missing"};CHECK_FALSE(chat.format(p,noName));auto before=chat.tableLoads();CHECK_FALSE(chat.format(p,noName));CHECK(chat.tableLoads()==before);
}
TEST_CASE("reference paths and resource cache loads stay bounded and missing data is never invented"){
 size_t reads=0;LocalizedChat chat([&](const std::string& path){++reads;return resource(path);});swgproto::StringIdChatParameter p;
 for(const auto& file:{"../base_player","/base_player","base_player/","base_player//sub","base_player\\sub"}){p.id={file,"tip_syntax"};CHECK_FALSE(chat.format(p,noName));}CHECK(reads==0);
 for(int i=0;i<32;++i){chat.beginPass();p.id={"missing_native_"+std::to_string(i),"key"};CHECK_FALSE(chat.format(p,noName));CHECK(chat.cachedTables()<=16);CHECK(chat.cachedTextBytes()<=8*1024*1024);}
 chat.beginPass();auto before=reads;for(int i=100;i<110;++i){p.id={"missing_native_"+std::to_string(i),"key"};CHECK_FALSE(chat.format(p,noName));}CHECK(reads-before==4);
}

TEST_CASE("unsupported original perspective and floating tokens defer while numeric suffixes remain literal"){
 LocalizedChat chat(resource);swgproto::StringIdChatParameter p;p.id={"error_message","prose_at_xp_limit"};p.to.text=u"Combat";p.di=100;
 CHECK(chat.format(p,noName)==u"You cannot gain anymore 'Combat' experience. (limit: 100pts)");
 chat.beginPass();p.id={"socials","wave"};CHECK_FALSE(chat.format(p,noName));
 chat.beginPass();p.id={"cbt_spam","dizzy_fall_down"};CHECK_FALSE(chat.format(p,noName));
 chat.beginPass();p.id={"event_perk","racing_lap_time"};CHECK_FALSE(chat.format(p,noName));
}

TEST_CASE("successful historic resolutions survive resource eviction and unresolved lines never starve later tables"){
 LocalizedChat chat([](const std::string&)->std::optional<std::vector<uint8_t>>{return resource("string/en/pvp_rating.stf");});
 swgproto::StringIdChatParameter missing;missing.id={"native_table_0","pvp_rating_target"};missing.tt.objectId=0x100000001;
 size_t count=0;for(int pass=0;pass<12;++pass){chat.beginPass();CHECK_FALSE(chat.formatRetained(1,missing,noName));count=0;
  for(uint64_t sequence=2;sequence<=34;++sequence){swgproto::StringIdChatParameter p;p.id={"native_table_"+std::to_string(sequence),"pvp_rating"};p.di=uint32_t(sequence);
   auto text=chat.formatRetained(sequence,p,noName);if(text){++count;CHECK(text==std::u16string(u"Your player vs. player combat rating is ")+[&](){auto n=std::to_string(sequence);return std::u16string(n.begin(),n.end());}()+u".");}
  }
 }
 CHECK(count==33);CHECK(chat.resolvedLines()==33);CHECK(chat.cachedTables()==16);
 chat.beginPass();auto loads=chat.tableLoads();for(uint64_t sequence=2;sequence<=34;++sequence){swgproto::StringIdChatParameter p;CHECK(chat.formatRetained(sequence,p,noName));}CHECK(chat.tableLoads()==loads);
 std::array<uint64_t,1> active{34};chat.retainSequences(active);CHECK(chat.resolvedLines()==1);
}
TEST_CASE("late object names update presentation without another network packet and stay stable after departure"){
 LocalizedChat chat(resource);swgproto::StringIdChatParameter p;p.id={"pvp_rating","pvp_rating_target"};p.tt.objectId=0x200000001;p.di=42;
 clientcommon::ChatSnapshot raw;raw.revision=77;clientcommon::ChatLine line;line.sequence=77;line.localized=p;raw.lines.push_back(line);clientcommon::ChatSnapshot display;
 auto attempt=[&](const LocalizedChat::ObjectName& names){auto next=raw;auto text=chat.formatRetained(77,p,names);next.lines[0].text=text.value_or(u"");return clientcommon::updateChatPresentation(display,std::move(next));};
 CHECK_FALSE(attempt(noName));CHECK(display.lines.empty());CHECK(display.revision==0);
 CHECK(attempt([](uint64_t)->std::optional<std::u16string>{return u"Novi";}));REQUIRE(display.lines.size()==1);CHECK(display.lines[0].text==u"Novi has a player vs. player combat rating of 42.");CHECK(display.revision==1);CHECK(raw.revision==77);
 CHECK_FALSE(attempt(noName));CHECK(display.revision==1);CHECK(display.lines[0].text==u"Novi has a player vs. player combat rating of 42.");
}

TEST_CASE("many permanent missing tables cannot starve incoming text or older late names"){
 LocalizedChat chat(resource);clientcommon::ChatSnapshot raw;raw.revision=50;clientcommon::ChatResolveSchedule schedule;
 for(uint64_t sequence=1;sequence<=20;++sequence){clientcommon::ChatLine line;line.sequence=sequence;line.localized.emplace();line.localized->id={"missing_native_"+std::to_string(sequence),"key"};raw.lines.push_back(line);}
 clientcommon::ChatLine incoming;incoming.sequence=21;incoming.localized.emplace();incoming.localized->id={"pvp_rating","pvp_rating"};raw.lines.push_back(incoming);
 std::set<size_t> starts;bool valid=false;
 for(int pass=0;pass<21;++pass){chat.beginPass();auto indices=schedule.order(raw);REQUIRE(indices.size()==21);starts.insert(indices[0]);
  if(pass==0)CHECK(indices[0]==20);
  for(auto i:indices){auto text=chat.formatRetained(raw.lines[i].sequence,*raw.lines[i].localized,noName);if(i==20){REQUIRE(text);CHECK(text==u"Your player vs. player combat rating is 0.");valid=true;}else CHECK_FALSE(text);}
 }
 CHECK(valid);CHECK(starts.size()==21);CHECK(raw.lines.back().sequence==21);
 ++raw.revision;CHECK(schedule.order(raw)[0]==20);
}

TEST_CASE("every retained line gets first resource admission even for histories sharing factors with the load budget"){
 for(size_t count:{20u,24u,256u}){clientcommon::ChatSnapshot raw;raw.revision=10;raw.lines.resize(count);clientcommon::ChatResolveSchedule schedule;std::set<size_t> starts;
  for(size_t pass=0;pass<count;++pass){auto order=schedule.order(raw);REQUIRE(order.size()==count);starts.insert(order.front());}
  CHECK(starts.size()==count);++raw.revision;CHECK(schedule.order(raw).front()==count-1);
 }
}
