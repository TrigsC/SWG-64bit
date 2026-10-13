#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "soe/ReliableChannel.h"
using namespace std::chrono_literals;
using C=soe::ReliableChannel;using Bytes=C::Bytes;
const C::Time t{100s};
Bytes wire(uint16_t seq){return {0,9,uint8_t(seq>>8),uint8_t(seq),42,0,0};}
TEST_CASE("Transmit window, cumulative ACK and unsent/future ACK controls") {
    C::Limits limits;limits.sendWindow=2;limits.maxQueued=4;C c(limits);c.start(t);
    c.queue({wire(0),wire(1),wire(2),wire(3)});
    CHECK(c.nextService(t)==t);CHECK(c.transmissions(t).size()==2);CHECK(c.pending()==4);
    CHECK_FALSE(c.acknowledge(2));CHECK(c.pending()==4);CHECK_FALSE(c.acknowledge(65535));
    CHECK(c.acknowledge(1));CHECK(c.pending()==2);CHECK(c.stats().acknowledged==2);
    auto next=c.transmissions(t+1ms);REQUIRE(next.size()==2);CHECK(next[0].wire==wire(2));CHECK(next[1].wire==wire(3));
    CHECK_FALSE(c.acknowledge(0));CHECK(c.acknowledge(3));CHECK(c.pending()==0);
}
TEST_CASE("Retry cadence is bounded, preserves bytes and stops after ACK") {
    C c;c.start(t);c.queue({wire(0)});REQUIRE(c.transmissions(t).size()==1);
    CHECK(c.transmissions(t+499ms).empty());auto retry=c.transmissions(t+500ms);REQUIRE(retry.size()==1);
    CHECK(retry[0].retry);CHECK(retry[0].wire==wire(0));CHECK(c.nextService(t+500ms)==t+1500ms);
    CHECK(c.transmissions(t+1499ms).empty());CHECK(c.transmissions(t+1500ms).size()==1);
    CHECK(c.nextService(t+1500ms)==t+3500ms);CHECK(c.transmissions(t+3500ms).size()==1);
    CHECK(c.nextService(t+3500ms)==t+5500ms);CHECK(c.stats().retransmissions==3);
    CHECK(c.acknowledge(0));CHECK(c.transmissions(t+6s).empty());
}
TEST_CASE("Sender sequence wrap and stale ACK cannot free the live window") {
    C c(C::Limits{},65534);c.start(t);c.queue({wire(65534),wire(65535),wire(0)});
    CHECK(c.nextSendSequence()==1);CHECK(c.transmissions(t).size()==3);
    CHECK(c.acknowledge(65535));CHECK(c.pending()==1);CHECK_FALSE(c.acknowledge(65534));
    CHECK_FALSE(c.acknowledge(1));CHECK(c.acknowledge(0));CHECK(c.pending()==0);
}
TEST_CASE("Receiver wrap, reordered data and duplicates preserve exact order") {
    C c(C::Limits{},0,65534);c.start(t);
    auto ahead=c.receive({0x0900,0,{'C'}},t);CHECK(ahead.frames.empty());CHECK_FALSE(ahead.ack);CHECK(ahead.gap==0);
    auto repeated=c.receive({0x0900,0,{'C'}},t+1ms);CHECK(repeated.frames.empty());CHECK_FALSE(repeated.ack);
    auto earlier=c.receive({0x0D00,65535,{'B'}},t+2ms);CHECK(earlier.frames.empty());CHECK_FALSE(earlier.gap);
    auto closed=c.receive({0x0D00,65534,{'A'}},t+3ms);REQUIRE(closed.frames.size()==3);
    CHECK(closed.frames[0].payload==Bytes{'A'});CHECK(closed.frames[1].payload==Bytes{'B'});CHECK(closed.frames[2].payload==Bytes{'C'});
    CHECK(closed.ack==0);CHECK(c.buffered()==0);CHECK(c.stats().outOfOrder==2);CHECK(c.stats().duplicates==1);
    auto old=c.receive({0x0900,65535,{'B'}},t+4ms);CHECK(old.frames.empty());CHECK(old.ack==0);
}
TEST_CASE("Queue and receive memory limits reject atomically without losing good data") {
    C::Limits limits;limits.sendWindow=2;limits.maxQueued=4;limits.receiveWindow=4;limits.maxBufferedBytes=4;
    C c(limits);c.start(t);c.queue({wire(0),wire(1),wire(2)});
    CHECK_THROWS(c.queue({wire(3),wire(4)}));CHECK(c.pending()==3);CHECK(c.nextSendSequence()==3);
    CHECK_THROWS(c.queue({Bytes(497)}));CHECK(c.pending()==3);CHECK(c.nextSendSequence()==3);
    CHECK(c.receive({0x0900,1,{'B','B','B'}},t).frames.empty());CHECK(c.buffered()==1);
    CHECK(c.receive({0x0900,2,{'C','C','C'}},t+1ms).frames.empty());CHECK(c.buffered()==1);
    CHECK(c.receive({0x0900,4,{'D'}},t+2ms).frames.empty());CHECK(c.stats().rejected==2);
    auto frames=c.receive({0x0900,0,{'A'}},t+3ms);REQUIRE(frames.frames.size()==2);CHECK(frames.frames[1].payload==Bytes{'B','B','B'});
    CHECK(c.buffered()==0);
}
TEST_CASE("Quiet peer, unacknowledged data and missing incoming gaps fail distinctly") {
    C quiet;quiet.start(t);CHECK_NOTHROW(quiet.checkTimeouts(t+59999ms));CHECK_THROWS_WITH(quiet.checkTimeouts(t+60s),"No validated server traffic within peer timeout");
    C pending;pending.start(t);pending.queue({wire(0)});pending.transmissions(t);pending.notePeer(t+29s);
    CHECK_THROWS_WITH(pending.checkTimeouts(t+30s),"Reliable data was not acknowledged within delivery timeout");
    C gap;gap.start(t);gap.receive({0x0900,1,{'B'}},t);gap.notePeer(t+29s);
    CHECK_THROWS_WITH(gap.checkTimeouts(t+30s),"Missing incoming sequence was not recovered within delivery timeout");
}
TEST_CASE("Repeated buffered duplicates cannot postpone a missing-sequence timeout") {
    C c;c.start(t);c.receive({0x0900,1,{'B'}},t);
    for(int i=1;i<30;++i){c.notePeer(t+i*1s);c.receive({0x0900,1,{'B'}},t+i*1s);}
    CHECK(c.stats().duplicates==29);CHECK_THROWS(c.checkTimeouts(t+30s));
}
TEST_CASE("Many complete sequence cycles preserve acknowledgements and delivery") {
    C c;c.start(t);
    for(uint32_t i=0;i<131080;++i) {
        uint16_t seq=i;c.queue({wire(seq)});auto sent=c.transmissions(t);
        REQUIRE(sent.size()==1);REQUIRE(sent[0].wire==wire(seq));REQUIRE(c.acknowledge(seq));
        auto delivered=c.receive({0x0900,seq,{uint8_t(i)}},t);REQUIRE(delivered.frames.size()==1);REQUIRE(delivered.ack==seq);
    }
    CHECK(c.pending()==0);CHECK(c.buffered()==0);CHECK(c.stats().acknowledged==131080);
}
