#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace soe {
// A local bounded-delivery policy, interoperating with SOE cumulative ACKs.
// No socket, game state, credentials, or clock calls: caller supplies time.
class ReliableChannel {
public:
    using Clock=std::chrono::steady_clock;
    using Time=Clock::time_point;
    using Bytes=std::vector<uint8_t>;
    struct Limits {
        size_t sendWindow=50, receiveWindow=512, maxQueued=512;
        size_t maxBufferedBytes=4*1024*1024;
        std::chrono::milliseconds retryInitial{500},retryMaximum{2000};
        std::chrono::milliseconds deliveryTimeout{30000},peerTimeout{60000};
    };
    struct Frame {uint16_t opcode,sequence;Bytes payload;};
    struct Delivery {std::vector<Frame> frames;std::optional<uint16_t> ack,gap;};
    struct Transmission {Bytes wire;bool retry;};
    struct Stats {uint32_t retransmissions=0,acknowledged=0,duplicates=0,outOfOrder=0,rejected=0;};
    ReliableChannel();
    explicit ReliableChannel(Limits limits,uint16_t initialSend=0,uint16_t initialReceive=0);
    void start(Time now);
    void notePeer(Time now){lastPeer_=now;}
    uint16_t nextSendSequence() const{return nextSend_;}
    // Limit/size validation failures consume no sequence or queue prefix.
    void queue(std::vector<Bytes> wires);
    std::vector<Transmission> transmissions(Time now);
    bool acknowledge(uint16_t sequence);
    Delivery receive(Frame frame,Time now);
    void checkTimeouts(Time now) const;
    Time nextService(Time now) const;
    const Stats& stats() const{return stats_;}
    size_t pending() const{return outgoing_.size();}
    size_t buffered() const{return incoming_.size();}
    const Limits& limits() const{return limits_;}
private:
    struct Pending {uint16_t sequence;Bytes wire;Time first{},last{};uint32_t sends=0;};
    std::chrono::milliseconds retryDelay(uint32_t sends) const;
    Limits limits_;
    uint16_t nextSend_,expectedReceive_;
    bool started_=false,delivered_=false;
    Time lastPeer_{};
    std::optional<Time> gapStarted_,lastGapHint_;
    std::deque<Pending> outgoing_;
    std::unordered_map<uint16_t,Frame> incoming_;
    size_t bufferedBytes_=0;
    Stats stats_;
};
}
