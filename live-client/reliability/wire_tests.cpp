#include "soe/SoeSession.h"
#include "soe/PacketBuffer.h"
#include "soe/Crc32.h"
#include "soe/XorCipher.h"
#include "soe/Compression.h"
#include <iostream>
#include <thread>
#include <functional>
#include <optional>
using namespace std::chrono_literals;
using Bytes=std::vector<uint8_t>;using udp=asio::ip::udp;
static int checks=0;
void require(bool ok,const char* reason){++checks;if(!ok)throw std::runtime_error(reason);}
constexpr uint32_t seed=0x12345678;
Bytes closeWire(soe::PacketBuffer p){p.writeByte(0);p.writeUint16(0);soe::XorCipher::encrypt(p,seed);soe::PacketCrc::append(p,seed);return {p.data(),p.data()+p.size()};}
Bytes channel(uint16_t seq,const Bytes& payload,uint16_t opcode=0x0900){soe::PacketBuffer p;p.writeUint16(opcode);p.writeUint16BE(seq);p.writeBytes(payload);return closeWire(p);}
Bytes control(uint16_t opcode,uint16_t seq){soe::PacketBuffer p;p.writeUint16(opcode);p.writeUint16BE(seq);return closeWire(p);}
struct Peer {
    asio::io_context io;udp::socket socket{io,{asio::ip::address_v4::loopback(),0}};udp::endpoint client;
    Peer(){socket.non_blocking(true);}
    uint16_t port(){return socket.local_endpoint().port();}
    std::optional<Bytes> read(std::chrono::milliseconds timeout=50ms){
        auto end=std::chrono::steady_clock::now()+timeout;
        do{Bytes raw(2048);std::error_code ec;auto n=socket.receive_from(asio::buffer(raw),client,0,ec);
            if(!ec){raw.resize(n);return raw;}if(ec!=asio::error::would_block&&ec!=asio::error::try_again)throw std::runtime_error(ec.message());
            std::this_thread::sleep_for(1ms);
        }while(std::chrono::steady_clock::now()<end);return {};
    }
    Bytes need(std::chrono::milliseconds timeout=100ms){auto raw=read(timeout);if(!raw)throw std::runtime_error("Peer did not receive expected datagram");return *raw;}
    void send(const Bytes& raw){socket.send_to(asio::buffer(raw),client);}
    void connect(soe::SoeSession& session){std::exception_ptr error;std::jthread task([&]{try{
        auto raw=need(1s);soe::PacketBuffer p(raw.data(),raw.size());if(p.readUint16()!=0x0100)throw std::runtime_error("Missing handshake");
        p.readUint16();p.readUint16BE();auto id=p.readUint32();soe::PacketBuffer reply;reply.writeUint16(0x0200);reply.writeUint32(id);reply.writeUint32BE(seed);
        for(int i=0;i<7;++i)reply.writeByte(0);send({reply.data(),reply.data()+reply.size()});
    }catch(...){error=std::current_exception();}});session.connect(1s);task.join();if(error)std::rethrow_exception(error);}
    void drain(){while(read(2ms)){};}
};
void quiet(soe::SoeSession& s,std::chrono::milliseconds timeout){try{require(s.receiveMessages(timeout).empty(),"Unexpected application delivery");}catch(const soe::TimeoutError&){};}
void gap(){Peer p;asio::io_context io;soe::SoeSession s(io,"127.0.0.1",p.port());p.connect(s);
    p.send(channel(1,{'B'}));require(s.receiveMessages(100ms).empty(),"Ahead-of-gap packet was delivered before sequence zero");
    auto hint=p.need();soe::PacketBuffer h(hint.data(),hint.size());soe::XorCipher::decrypt(h,seed);
    require(h.readUint16()==0x1100&&h.readUint16BE()==1,"Gap needs an out-of-order hint, not an ACK past missing data");
    p.send(channel(0,{'A'}));auto result=s.receiveMessages(100ms);require(result==std::vector<Bytes>{{'A'},{'B'}},"Gap closure must deliver A then B exactly once");
}
void duplicate(){Peer p;asio::io_context io;soe::SoeSession s(io,"127.0.0.1",p.port());p.connect(s);
    p.send(channel(0,{'A'}));require(s.receiveMessages(100ms)==std::vector<Bytes>{{'A'}},"Initial packet not delivered");p.drain();
    p.send(channel(0,{'A'}));require(s.receiveMessages(100ms).empty(),"Duplicate application event delivered twice");
    auto wire=p.need();require(soe::PacketCrc::test(soe::PacketBuffer(wire.data(),wire.size()),seed),"Duplicate ACK CRC invalid");
}
void retry(){Peer p;asio::io_context io;soe::SoeSession s(io,"127.0.0.1",p.port());p.connect(s);
    s.sendMessage({5,0,'A'});auto original=p.need();quiet(s,650ms);auto resent=p.need();require(resent==original,"Lost data must retry identical wire bytes and sequence");
    p.send(control(0x1500,0));quiet(s,100ms);p.drain();quiet(s,1100ms);require(!p.read(10ms),"Acknowledged packet was retransmitted");
}
void lostAck(){Peer p;asio::io_context io;soe::SoeSession s(io,"127.0.0.1",p.port());p.connect(s);
    s.sendMessage({'A'});p.need();quiet(s,650ms);auto retry=p.need();require(retry==channel(0,{'A'}),"Lost ACK retry changed sequence or payload");
    s.sendMessage({'B'});require(p.need()==channel(1,{'B'}),"Retry must not consume a sequence");
    p.send(control(0x1500,1));quiet(s,100ms);p.drain();quiet(s,1100ms);require(!p.read(10ms),"Cumulative ACK did not clear earlier pending packet");
}
void fragments(){Peer p;asio::io_context io;soe::SoeSession s(io,"127.0.0.1",p.port());p.connect(s);
    soe::PacketBuffer first;first.writeUint32BE(6);first.writeBytes(Bytes{'A','B'});p.send(channel(0,{first.data(),first.data()+first.size()},0x0D00));quiet(s,100ms);p.drain();
    p.send(channel(2,{'E','F'},0x0D00));quiet(s,100ms);p.drain();
    p.send(channel(1,{'C','D'},0x0D00));require(s.receiveMessages(100ms)==std::vector<Bytes>{{'A','B','C','D','E','F'}},"Out-of-order fragments lost the completed message");
}
void multi(){Peer p;asio::io_context io;soe::SoeSession s(io,"127.0.0.1",p.port());p.connect(s);
    soe::PacketBuffer outer;outer.writeUint16(0x0300);
    for(int i=0;i<2;++i){soe::PacketBuffer child;child.writeUint16(0x0900);child.writeUint16BE(i);child.writeByte('A'+i);outer.writeByte(child.size());outer.writeBytes(child.data(),child.size());}
    p.send(closeWire(outer));require(s.receiveMessages(100ms)==std::vector<Bytes>{{'A'},{'B'}},"SOE multipacket did not dispatch its sequenced children");
}

void outboundFragments(){Peer p;asio::io_context io;soe::SoeSession s(io,"127.0.0.1",p.port());p.connect(s);
    Bytes payload(1200);for(size_t i=0;i<payload.size();++i)payload[i]=i;
    s.sendMessage(payload);Bytes joined;
    for(int i=0;i<3;++i){auto raw=p.need();require(raw.size()<=496,"Outgoing fragment exceeds negotiated datagram size");
        soe::PacketBuffer f(raw.data(),raw.size());require(soe::PacketCrc::test(f,seed),"Outgoing fragment CRC invalid");soe::XorCipher::decrypt(f,seed);f.removeLastBytes(3);
        require(f.readUint16()==0x0D00&&f.readUint16BE()==i,"Outgoing fragment sequence wrong");if(i==0)require(f.readUint32BE()==1200,"First fragment length wrong");
        auto bytes=f.readBytes(f.remaining());joined.insert(joined.end(),bytes.begin(),bytes.end());}
    require(joined==payload,"Outgoing fragmented message changed bytes");p.send(control(0x1500,2));quiet(s,100ms);require(s.transportStats().pendingPackets==0,"Fragment ACK did not clear send window");
}
void compressed(){Peer p;asio::io_context io;soe::SoeSession s(io,"127.0.0.1",p.port());p.connect(s);
    soe::PacketBuffer body;body.writeUint16BE(0);body.writeBytes(Bytes{'A','B','C'});
    auto zipped=soe::Compression::compress(body.data(),body.size());soe::PacketBuffer packet;packet.writeUint16(0x0900);packet.writeBytes(zipped);packet.writeByte(1);packet.writeUint16(0);
    soe::XorCipher::encrypt(packet,seed);soe::PacketCrc::append(packet,seed);p.send({packet.data(),packet.data()+packet.size()});
    require(s.receiveMessages(100ms)==std::vector<Bytes>{{'A','B','C'}},"Compressed packet was not ordered/decompressed correctly");
}
void badAck(){Peer p;asio::io_context io;soe::SoeSession s(io,"127.0.0.1",p.port());p.connect(s);s.sendMessage({'A'});p.need();
    auto bad=control(0x1500,0);bad.back()^=1;p.send(bad);quiet(s,100ms);require(s.transportStats().pendingPackets==1,"Bad-CRC ACK freed live data");
    p.send(control(0x1500,1));quiet(s,100ms);require(s.transportStats().pendingPackets==1,"Future ACK freed live data");
    p.send(control(0x1500,0));quiet(s,100ms);require(s.transportStats().pendingPackets==0,"Correct ACK did not free data");
}
void malformedMulti(){Peer p;asio::io_context io;soe::SoeSession s(io,"127.0.0.1",p.port());p.connect(s);
    soe::PacketBuffer packet;packet.writeUint16(0x0300);packet.writeByte(5);packet.writeUint16(0x0900);packet.writeUint16BE(0);packet.writeByte('A');
    packet.writeByte(9);packet.writeUint16(0x0900);p.send(closeWire(packet));quiet(s,100ms);
    require(!p.read(10ms),"Malformed multipacket mutated the ACK window");require(s.transportStats().invalidPackets==1,"Malformed multipacket not diagnosed");
    p.send(channel(0,{'B'}));require(s.receiveMessages(100ms)==std::vector<Bytes>{{'B'}},"Malformed prefix consumed receive sequence");
}
soe::ReliableChannel::Limits fastLimits(){soe::ReliableChannel::Limits limits;limits.retryInitial=50ms;limits.retryMaximum=100ms;limits.deliveryTimeout=350ms;limits.peerTimeout=600ms;return limits;}
void expectFailure(soe::SoeSession& s,const std::string& reason,std::chrono::milliseconds time=800ms){
    bool failed=false;try{s.receiveMessages(time);}catch(const soe::DisconnectedError& e){failed=std::string(e.what()).find(reason)!=std::string::npos;}
    require(failed,"Session did not report the expected bounded failure");
}
void quietPeer(){Peer p;asio::io_context io;soe::SoeSession s(io,"127.0.0.1",p.port(),fastLimits());p.connect(s);
    expectFailure(s,"No validated server traffic");
    bool closed=false;try{s.sendMessage({'X'});}catch(const soe::DisconnectedError&){closed=true;}require(closed,"Failed session accepted a new command");
}
void ackTimeout(){Peer p;asio::io_context io;soe::SoeSession s(io,"127.0.0.1",p.port(),fastLimits());p.connect(s);s.sendMessage({'A'});p.need();
    p.send(channel(0,{'B'}));require(s.receiveMessages(100ms)==std::vector<Bytes>{{'B'}},"Missing ACK control did not receive live data");
    expectFailure(s,"not acknowledged");
}
void gapTimeout(){Peer p;asio::io_context io;soe::SoeSession s(io,"127.0.0.1",p.port(),fastLimits());p.connect(s);
    p.send(channel(1,{'B'}));quiet(s,100ms);expectFailure(s,"Missing incoming sequence");
}
void fragmentTimeout(){Peer p;asio::io_context io;soe::SoeSession s(io,"127.0.0.1",p.port(),fastLimits());p.connect(s);
    soe::PacketBuffer first;first.writeUint32BE(10);first.writeByte('A');p.send(channel(0,{first.data(),first.data()+first.size()},0x0D00));quiet(s,100ms);
    expectFailure(s,"Fragmented message stopped");
}
int main(){int failed=0;
    const std::vector<std::pair<const char*,std::function<void()>>> cases={
        {"receive_gap",gap},{"duplicate_delivery",duplicate},{"lost_data",retry},{"lost_ack_cumulative",lostAck},{"reordered_fragments",fragments},{"soe_multipacket",multi},
        {"outbound_fragmentation",outboundFragments},{"compressed_data",compressed},{"invalid_future_ack",badAck},{"malformed_multipacket_atomic",malformedMulti},
        {"quiet_peer_failure",quietPeer},{"unacknowledged_failure",ackTimeout},{"missing_sequence_failure",gapTimeout},{"incomplete_fragment_failure",fragmentTimeout}};
    for(auto [name,test]:cases){
        try{test();std::cout<<"{\"case\":\""<<name<<"\",\"passed\":true}\n";}
        catch(const std::exception& e){++failed;std::cout<<"{\"case\":\""<<name<<"\",\"passed\":false,\"reason\":\""<<e.what()<<"\"}\n";}
    }
    std::cout<<"{\"cases\":"<<cases.size()<<",\"failed\":"<<failed<<",\"checks\":"<<checks<<"}\n";return failed?1:0;
}
