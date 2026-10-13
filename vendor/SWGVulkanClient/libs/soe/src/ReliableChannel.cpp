#include "soe/ReliableChannel.h"
#include <algorithm>
#include <utility>
namespace soe {
ReliableChannel::ReliableChannel():ReliableChannel(Limits{}){}
ReliableChannel::ReliableChannel(Limits limits,uint16_t send,uint16_t receive)
    :limits_(limits),nextSend_(send),expectedReceive_(receive){
    if(!limits.sendWindow||limits.sendWindow>limits.maxQueued||limits.maxQueued>=32768||
       !limits.receiveWindow||limits.receiveWindow>=32768||!limits.maxBufferedBytes||
       limits.retryInitial.count()<=0||limits.retryMaximum<limits.retryInitial||
       limits.deliveryTimeout<=limits.retryMaximum||limits.peerTimeout.count()<=0)
        throw std::invalid_argument("Invalid reliable transport limits");
}
void ReliableChannel::start(Time now){started_=true;lastPeer_=now;}
void ReliableChannel::queue(std::vector<Bytes> wires){
    if(wires.size()>limits_.maxQueued-outgoing_.size())throw std::runtime_error("Reliable send queue is full");
    for(const auto& wire:wires)if(wire.size()<7||wire.size()>496)throw std::invalid_argument("Reliable datagram size outside negotiated bounds");
    for(auto& wire:wires){outgoing_.push_back({nextSend_,std::move(wire)});++nextSend_;}
}
std::chrono::milliseconds ReliableChannel::retryDelay(uint32_t sends) const {
    auto delay=limits_.retryInitial;
    for(uint32_t i=1;i<sends&&delay<limits_.retryMaximum;++i)
        delay=std::min(limits_.retryMaximum,delay*2);
    return delay;
}
void ReliableChannel::checkTimeouts(Time now) const {
    if(started_&&now-lastPeer_>=limits_.peerTimeout)throw std::runtime_error("No validated server traffic within peer timeout");
    if(!outgoing_.empty()&&outgoing_.front().sends&&now-outgoing_.front().first>=limits_.deliveryTimeout)
        throw std::runtime_error("Reliable data was not acknowledged within delivery timeout");
    if(gapStarted_&&now-*gapStarted_>=limits_.deliveryTimeout)
        throw std::runtime_error("Missing incoming sequence was not recovered within delivery timeout");
}
std::vector<ReliableChannel::Transmission> ReliableChannel::transmissions(Time now){
    checkTimeouts(now);
    std::vector<Transmission> ready;
    size_t active=0;
    for(auto& pending:outgoing_){
        if(pending.sends)++active;
        else if(active>=limits_.sendWindow)break;
        else ++active;
        if(!pending.sends||now-pending.last>=retryDelay(pending.sends)){
            bool retry=pending.sends!=0;
            if(!retry)pending.first=now;
            else ++stats_.retransmissions;
            pending.last=now;++pending.sends;ready.push_back({pending.wire,retry});
        }
    }
    return ready;
}
bool ReliableChannel::acknowledge(uint16_t sequence){
    if(outgoing_.empty())return false;
    const size_t distance=uint16_t(sequence-outgoing_.front().sequence);
    // Includes wrap; only an ACK for a transmitted member of this bounded
    // window can free data. Future ACKs cannot discard queued/unsent packets.
    if(distance>=outgoing_.size()||!outgoing_[distance].sends)return false;
    for(size_t i=0;i<=distance;++i){outgoing_.pop_front();++stats_.acknowledged;}
    return true;
}
ReliableChannel::Delivery ReliableChannel::receive(Frame frame,Time now){
    Delivery result;
    const auto distance=uint16_t(frame.sequence-expectedReceive_);
    if(distance>=32768){++stats_.duplicates;if(delivered_)result.ack=uint16_t(expectedReceive_-1);return result;}
    if(distance>=limits_.receiveWindow||frame.payload.size()>8192){++stats_.rejected;return result;}
    if(distance){
        if(incoming_.contains(frame.sequence)){++stats_.duplicates;return result;}
        if(frame.payload.size()>limits_.maxBufferedBytes-bufferedBytes_){++stats_.rejected;return result;}
        bufferedBytes_+=frame.payload.size();incoming_.emplace(frame.sequence,std::move(frame));++stats_.outOfOrder;
        if(!gapStarted_)gapStarted_=now;
        if(!lastGapHint_||now-*lastGapHint_>=limits_.retryInitial){result.gap=uint16_t(expectedReceive_+distance);lastGapHint_=now;}
        return result;
    }
    result.frames.push_back(std::move(frame));++expectedReceive_;delivered_=true;
    for(;;){
        auto it=incoming_.find(expectedReceive_);if(it==incoming_.end())break;
        bufferedBytes_-=it->second.payload.size();result.frames.push_back(std::move(it->second));incoming_.erase(it);++expectedReceive_;
    }
    if(incoming_.empty()){gapStarted_.reset();lastGapHint_.reset();}
    else gapStarted_=now; // a recovered hole made forward progress
    result.ack=uint16_t(expectedReceive_-1);
    return result;
}
ReliableChannel::Time ReliableChannel::nextService(Time now) const{
    auto next=started_?lastPeer_+limits_.peerTimeout:Time::max();
    if(gapStarted_)next=std::min(next,*gapStarted_+limits_.deliveryTimeout);
    size_t active=0;
    for(const auto& pending:outgoing_){
        if(!pending.sends){if(active<limits_.sendWindow)return now;break;}
        ++active;next=std::min(next,pending.last+retryDelay(pending.sends));
    }
    if(!outgoing_.empty()&&outgoing_.front().sends)next=std::min(next,outgoing_.front().first+limits_.deliveryTimeout);
    return next;
}
}
