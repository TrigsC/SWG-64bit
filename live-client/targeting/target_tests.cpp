#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "clientcommon/TargetSelection.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <fstream>
using namespace clientcommon;
namespace {
InspectionTarget creature(uint64_t id,float x,float z) { return {id,u"Creature",{x,0,z},true,false}; }
}
TEST_CASE("Target cycle uses full IDs, nearest displayed distance, ties and wrap") {
    std::vector<InspectionTarget> targets{creature(0x100000002,2,0),creature(0x100000001,-2,0),creature(7,4,0)};
    auto self=creature(1,0,0);self.self=true;targets.push_back(self);
    targets.push_back(creature(0,0,0));targets.push_back(creature(8,65,0));
    TargetSelection s;s.cycle(targets,{});CHECK(s.id()==0x100000001);
    std::reverse(targets.begin(),targets.end());s.cycle(targets,{});CHECK(s.id()==0x100000002);
    s.cycle(targets,{});CHECK(s.id()==7);s.cycle(targets,{});CHECK(s.id()==0x100000001);
    CHECK(TargetSelection::distance(*s.selected(targets),{})==doctest::Approx(2));
}
TEST_CASE("Target lock survives camera changes, movement, range exit and unresolved position; destroy clears") {
    std::vector<InspectionTarget> targets{creature(9,1,2)};TargetSelection s;s.cycle(targets,{});
    targets[0].position={100,3,4};s.reconcile(targets);CHECK(s.id()==9);
    CHECK(TargetSelection::distance(targets[0],{})==doctest::Approx(std::sqrt(10025.f)));
    targets[0].resolved=false;s.reconcile(targets);CHECK(s.id()==9);
    CHECK_FALSE(TargetSelection::distance(targets[0],{}));
    targets.clear();s.reconcile(targets);CHECK(s.id()==0);CHECK_FALSE(s.selected(targets));
    targets.push_back(creature(9,0,1));s.reconcile(targets);CHECK(s.id()==0); // no resurrection
    s.cycle(targets,{});targets[0].self=true;s.reconcile(targets);CHECK(s.id()==0);
}
TEST_CASE("Cursor inspection finds nearest positive intersection with a non-unit ray") {
    std::vector<InspectionTarget> targets{creature(0x100000004,0,10),creature(0x100000003,0,5),creature(2,0,-3)};
    TargetSelection s;s.pick(targets,{}, {0,.9f,0},{0,0,4});CHECK(s.id()==0x100000003);
    s.pick(targets,{}, {0,.9f,6},{0,0,1});CHECK(s.id()==0x100000003); // origin inside sphere
    s.pick(targets,{}, {0,.9f,0},{1,0,0});CHECK(s.id()==0);
    s.pick(targets,{}, {0,.9f,0},{0,0,0});CHECK(s.id()==0);
    s.pick(targets,{}, {NAN,0,0},{0,0,1});CHECK(s.id()==0);
    targets[1].resolved=false;s.pick(targets,{}, {0,.9f,0},{0,0,1});CHECK(s.id()==0x100000004);
    targets[0].position.z=65;s.pick(targets,{}, {0,.9f,0},{0,0,1});CHECK(s.id()==0);
}
TEST_CASE("Malformed scene positions and missing candidates never become a guessed target") {
    std::vector<InspectionTarget> targets{creature(9,0,1)};targets[0].position.x=NAN;
    TargetSelection s;s.cycle(targets,{});CHECK(s.id()==0);
    CHECK_FALSE(TargetSelection::distance(targets[0],{}));
    targets[0].position.x=0;s.cycle(targets,{INFINITY,0,0});CHECK(s.id()==0);
    s.cycle(targets,{});REQUIRE(s.id()==9);s.clear();CHECK(s.id()==0);
    CHECK_FALSE(s.selected(targets));CHECK(targetDistanceText({})=="");
    CHECK(targetDistanceText(NAN)=="");CHECK(targetDistanceText(-1.f)=="");
    CHECK(targetDistanceText(0.f)=="0.0 m");CHECK(targetDistanceText(12.34f)=="12.3 m");
}
TEST_CASE("Names are bounded and Unicode conversion preserves supplementary characters") {
    CHECK(targetDisplayName(u"")==u"Unnamed creature");
    CHECK(targetDisplayName(u"A\n\tB")==u"AB");
    CHECK(targetDisplayName(u"Pilot \\#ffff00[SWGEmu-Admin]\\#.")==u"Pilot [SWGEmu-Admin]");
    CHECK(targetDisplayName(u"\\#Ab12EF\\#00ff00Pilot\\#.")==u"Pilot");
    CHECK(targetDisplayName(u"\\#ffff00\\#.")==u"Unnamed creature");
    CHECK(targetDisplayName(u"\\#12xz00 \\#12345 \\#?")==u"\\#12xz00 \\#12345 \\#?");
    auto name=targetDisplayName(u"Pilot \U0001F680");CHECK(name==u"Pilot \U0001F680");
    auto wide=targetWideText(name);
    if constexpr(sizeof(wchar_t)==4){CHECK(wide.size()==7);CHECK(uint32_t(wide.back())==0x1f680);}
    else CHECK(wide.size()==8);
    std::u16string bad{char16_t(0xd800),'x',char16_t(0xdc00)};
    CHECK(targetDisplayName(bad)==std::u16string{char16_t(0xfffd),'x',char16_t(0xfffd)});
    auto longName=std::u16string(63,'a')+u"\U0001F680extra";
    CHECK(targetDisplayName(longName).size()==65); // 64 scalars, final surrogate pair intact
}

#ifdef SWG_TARGET_FIXTURE
TEST_CASE("Recorded native scene agrees with independent scalar nearest ID and distance") {
    std::ifstream input(SWG_TARGET_FIXTURE);REQUIRE(input.good());
    TargetPoint self;uint64_t expected;double expectedDistance;
    REQUIRE(bool(input>>self.x>>self.y>>self.z>>expected>>expectedDistance));
    std::vector<InspectionTarget> targets;InspectionTarget t;
    while(input>>t.id>>t.position.x>>t.position.y>>t.position.z) {
        t.resolved=true;targets.push_back(t);
    }
    REQUIRE(targets.size()==6);
    TargetSelection selection;selection.cycle(targets,self);CHECK(selection.id()==expected);
    auto actual=selection.selected(targets);REQUIRE(actual);
    CHECK(*TargetSelection::distance(*actual,self)==doctest::Approx(expectedDistance).epsilon(.00001));
    std::reverse(targets.begin(),targets.end());selection.clear();selection.cycle(targets,self);
    CHECK(selection.id()==expected);CHECK(selection.id()>UINT32_MAX);
}
#endif
