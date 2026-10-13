#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "clientcommon/HamFeedback.h"
#include <limits>
using namespace clientcommon;
TEST_CASE("damage geometry uses pre-decay history and independent truncation") {
    HamFeedback h;auto g=h.update(HamBarStatus{1000,1000,0},1000,113,0);
    CHECK_FALSE(g.recentCurrentVisible);CHECK_FALSE(g.recentMaximumVisible);
    g=h.update(HamBarStatus{600,1000,0},1000,113,1.f/60);
    CHECK(g.bar.current==67);CHECK(g.recentCurrentX==67);CHECK(g.recentCurrentWidth==45);
    CHECK(g.recentCurrentVisible);CHECK(h.recentCurrent()==doctest::Approx(946.666687));
    g=h.update(HamBarStatus{600,1000,0},1000,113,1.f/60);
    CHECK(g.recentCurrentWidth==39);CHECK(h.recentCurrent()==doctest::Approx(900.444458));
    // Old visible width minus new current width would be46, an incorrect shortcut.
    CHECK(g.recentCurrentWidth!=113-g.bar.current);
}
TEST_CASE("wound histories retain the original edge position and current clamp") {
    HamFeedback h;h.update(HamBarStatus{900,1000,0},1000,113,0);
    auto g=h.update(HamBarStatus{500,1000,250},1000,113,.05f);
    CHECK(g.bar.normal==113);CHECK(g.bar.usable==84);CHECK(g.bar.current==56);
    CHECK(g.recentCurrentX==56);CHECK(g.recentCurrentWidth==28);
    CHECK(g.recentMaximumVisible);CHECK(g.recentMaximumX==113);CHECK(g.recentMaximumWidth==28);
    CHECK(h.recentMaximum()==doctest::Approx(950));CHECK(h.recentCurrent()==doctest::Approx(650));
    g=h.update(HamBarStatus{800,1000,0},1000,113,0);
    CHECK_FALSE(g.recentMaximumVisible);CHECK_FALSE(g.recentCurrentVisible);CHECK(h.recentCurrent()==800);
}
TEST_CASE("float histories settle within one unit and hide on the following call") {
    HamFeedback h;h.update(HamBarStatus{1000,1000,0},1000,115,0);
    h.update(HamBarStatus{500,1000,0},1000,115,0);
    bool snapped=false;
    for(int i=0;i<100;++i) {
        auto g=h.update(HamBarStatus{500,1000,0},1000,115,1.f/60);
        if(h.recentCurrent()==500){CHECK(g.recentCurrentVisible);snapped=true;break;}
    }
    CHECK(snapped);CHECK_FALSE(h.update(HamBarStatus{500,1000,0},1000,115,0).recentCurrentVisible);
}
TEST_CASE("large elapsed time preserves the two distinct overshoot rules") {
    HamFeedback h;h.update(HamBarStatus{1000,1000,0},1000,115,0);
    auto g=h.update(HamBarStatus{200,1000,500},1000,115,1);
    CHECK(g.recentCurrentVisible);CHECK(g.recentMaximumVisible);
    CHECK(h.recentCurrent()==200);CHECK(h.recentMaximum()==-1000);
    g=h.update(HamBarStatus{200,1000,500},1000,115,0);
    CHECK_FALSE(g.recentCurrentVisible);CHECK_FALSE(g.recentMaximumVisible);CHECK(h.recentMaximum()==500);
}
TEST_CASE("unknown rows reset and fresh observations cannot manufacture a transition") {
    HamFeedback h;h.update(HamBarStatus{1000,1000,0},1000,115,0);
    auto g=h.update({},1000,115,.01f);CHECK(g.bar.normal==0);
    g=h.update(HamBarStatus{200,1000,0},1000,115,.01f);CHECK_FALSE(g.recentCurrentVisible);
    h.reset();g=h.update(HamBarStatus{-1,1000,0},1000,115,0);CHECK(g.bar.current==0);CHECK_FALSE(g.recentCurrentVisible);
    g=h.update(HamBarStatus{0,1000,1000},1000,115,0);CHECK(g.bar.usable==0);CHECK_FALSE(g.recentCurrentVisible);
}
TEST_CASE("invalid timing freezes histories and signed current stays signed") {
    for(float dt:{-1.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        HamFeedback h;h.update(HamBarStatus{1000,1000,0},1000,113,0);
        auto g=h.update(HamBarStatus{-1,1000,0},1000,113,dt);
        CHECK(g.bar.current==0);CHECK(g.recentCurrentWidth==113);CHECK(h.recentCurrent()==1000);
    }
}
TEST_CASE("unequal pools use shared maximum before nested recent-current projection") {
    HamFeedback h;h.update(HamBarStatus{600,600,0},1000,113,0);
    auto g=h.update(HamBarStatus{100,600,150},1000,113,0);
    CHECK(g.bar.normal==67);CHECK(g.bar.usable==50);CHECK(g.bar.current==11);
    CHECK(g.recentCurrentX==11);CHECK(g.recentCurrentWidth==38);CHECK(g.recentMaximumX==67);
    CHECK(h.recentCurrent()==450);CHECK(h.recentMaximum()==600);
}
