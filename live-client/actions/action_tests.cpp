#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "clientcommon/ActionToolbar.h"

using namespace clientcommon;

TEST_CASE("Target and chat focus gate toolbar activation") {
    ActionToolbar bar;
    bar.configure(0,"attack",u"Attack",true,true);
    bar.configure(1,"peace",u"Peace",true,false);
    bar.configure(2,"kneel",u"Kneel",false,false);
    CHECK(bar.state(0,false,0)==ActionToolbar::State::NeedsTarget);
    CHECK_FALSE(bar.request(0,false,false,0));
    CHECK_FALSE(bar.request(0,true,true,0));
    CHECK(bar.request(0,true,false,0)=="attack");
    CHECK(bar.request(1,false,false,0)=="peace");
    CHECK_FALSE(bar.request(2,false,false,0));
    CHECK_FALSE(bar.request(4,true,false,0));
}

TEST_CASE("Queue admission blocks repeat input and only the matching server reply starts a timer") {
    ActionToolbar bar;bar.configure(0,"attack",u"Attack",true,true);
    CommandSubmission queue;
    const auto ticket=queue.reserve(0x100000001,0x100000002,"attack",100);
    REQUIRE(ticket);
    queue.admitted(*ticket,true);
    for(const auto& event:queue.drain(100))bar.onEvent(event,100);
    CHECK(bar.state(0,true,100)==ActionToolbar::State::Pending);
    CHECK_FALSE(bar.request(0,true,false,100));
    swgproto::CommandQueueRemove reply;reply.actionCount=ticket->action;reply.timer=1.5f;
    CHECK_FALSE(queue.receive(0x100000002,reply));
    CHECK(bar.state(0,true,200)==ActionToolbar::State::Pending);
    CHECK(queue.receive(0x100000001,reply));
    for(const auto& event:queue.drain(200))bar.onEvent(event,200);
    CHECK(bar.state(0,true,200)==ActionToolbar::State::Cooldown);
    CHECK_FALSE(bar.request(0,true,false,1699));
    CHECK(bar.request(0,true,false,1700)=="attack");
}

TEST_CASE("Rejection permits retry while no-reply remains explicitly unknown") {
    ActionToolbar bar;bar.configure(3,"stand",u"Stand",true,false);
    CommandSubmission queue;
    auto first=queue.reserve(42,0,"stand",0);REQUIRE(first);
    queue.admitted(*first,true);
    for(const auto& event:queue.drain(0))bar.onEvent(event,0);
    swgproto::CommandQueueRemove rejected;rejected.actionCount=first->action;rejected.tab1=2;rejected.timer=4.f;
    CHECK(queue.receive(42,rejected));
    for(const auto& event:queue.drain(100))bar.onEvent(event,100);
    CHECK(bar.state(3,false,100)==ActionToolbar::State::Rejected);
    CHECK(bar.slot(3).error==2);
    CHECK(bar.request(3,false,false,100)=="stand");
    auto retry=queue.reserve(42,0,"stand",1000);REQUIRE(retry);
    queue.admitted(*retry,true);
    for(const auto& event:queue.drain(1000))bar.onEvent(event,1000);
    CHECK(bar.state(3,false,1000)==ActionToolbar::State::Pending);
    for(const auto& event:queue.drain(11001))bar.onEvent(event,11001);
    CHECK(bar.state(3,false,11001)==ActionToolbar::State::Unknown);
    CHECK(bar.request(3,false,false,11001)=="stand");
}
