#include "soe/SoeSession.h"
#include "soe/PacketBuffer.h"
#include "soe/Crc32.h"
#include "soe/XorCipher.h"
#include <future>
#include <thread>
#include <iostream>
#include <stdexcept>
using namespace std::chrono_literals;
using udp=asio::ip::udp;
int checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
struct Peer {
    asio::io_context io;udp::socket socket{io,{asio::ip::address_v4::loopback(),0}};
    udp::endpoint client;std::exception_ptr error;std::thread thread;
    Peer(){socket.non_blocking(true);}
    ~Peer(){if(thread.joinable())thread.join();}
    std::vector<uint8_t> read(std::chrono::milliseconds timeout=2s){
        const auto deadline=std::chrono::steady_clock::now()+timeout;
        while(std::chrono::steady_clock::now()<deadline){
            std::vector<uint8_t> bytes(2048);std::error_code ec;auto n=socket.receive_from(asio::buffer(bytes),client,0,ec);
            if(!ec){bytes.resize(n);return bytes;}
            if(ec!=asio::error::would_block&&ec!=asio::error::try_again)throw std::runtime_error(ec.message());
            std::this_thread::sleep_for(1ms);
        }throw std::runtime_error("Loopback peer timed out");
    }
    void send(const soe::PacketBuffer& packet){socket.send_to(asio::buffer(packet.data(),packet.size()),client);}
    void response(const std::vector<uint8_t>& request,bool badId=false){
        if(request.size()!=14)throw std::runtime_error("Wrong real SOE request size");
        soe::PacketBuffer input(request.data(),request.size());
        if(input.readUint16()!=0x0100)throw std::runtime_error("Expected SessionRequest only");
        input.readUint16();input.readUint16BE();const auto id=input.readUint32();
        soe::PacketBuffer reply;reply.writeUint16(0x0200);reply.writeUint32(id+(badId?1:0));reply.writeUint32BE(0x12345678);
        for(int i=0;i<7;++i)reply.writeByte(0);send(reply);
    }
    void ack(){soe::PacketBuffer p;p.writeUint16(0x1500);p.writeUint16BE(0);p.writeByte(0);p.writeUint16(0);
        soe::XorCipher::encrypt(p,0x12345678);soe::PacketCrc::append(p,0x12345678);send(p);}
    template<class F>void start(F work){thread=std::thread([&,work]{try{work();}catch(...){error=std::current_exception();}});}
    void finish(){thread.join();if(error)std::rethrow_exception(error);}
    auto port()const{return socket.local_endpoint().port();}
};
int main(){try{
    {
        Peer peer;std::promise<void> timeoutObserved;auto signal=timeoutObserved.get_future();
        peer.start([&]{peer.response(peer.read());peer.ack();std::this_thread::sleep_for(20ms);peer.ack();
            if(signal.wait_for(2s)!=std::future_status::ready)throw std::runtime_error("No receive-timeout signal");peer.ack();
            const auto logout=peer.read();if(logout.size()<2||logout[0]!=0||logout[1]!=5)throw std::runtime_error("No graceful Disconnect");});
        asio::io_context io;soe::SoeSession client(io,"127.0.0.1",peer.port());client.connect(1s);
        require(client.crcSeed()==0x12345678,"Handshake CRC seed mismatch");
        require(client.receiveMessages(200ms).empty(),"Ack must not become gameplay data");
        require(client.receiveMessages(200ms).empty(),"Canceled old timer must not abort next receive");
        bool timedOut=false;try{client.receiveMessages(5ms);}catch(const soe::TimeoutError&){timedOut=true;}
        require(timedOut,"Quiet receive must time out");timeoutObserved.set_value();
        require(client.receiveMessages(200ms).empty(),"Receive must recover after timeout");
        client.disconnect();client.disconnect();peer.finish();++checks;
    }
    {
        Peer peer;peer.start([&]{peer.response(peer.read(),true);});asio::io_context io;
        soe::SoeSession client(io,"127.0.0.1",peer.port());bool rejected=false;
        try{client.connect(1s);}catch(const std::runtime_error&){rejected=true;}
        require(rejected,"Mismatched connection ID must not authenticate endpoint");peer.finish();
    }
    {
        Peer peer;peer.start([&]{peer.read();});asio::io_context io;soe::SoeSession client(io,"127.0.0.1",peer.port());
        bool timedOut=false;try{client.connect(20ms);}catch(const soe::TimeoutError&){timedOut=true;}
        require(timedOut,"Silent handshake must time out");peer.finish();
    }
    {
        Peer peer;
        peer.start([&]{
            peer.response(peer.read());
            const auto wire=peer.read(7s);
            if(wire.size()!=35)throw std::runtime_error("Wrong full net-status request size");
            soe::PacketBuffer request(wire.data(),wire.size());
            if(!soe::PacketCrc::test(request,0x12345678))throw std::runtime_error("Net-status request CRC invalid");
            soe::XorCipher::decrypt(request,0x12345678);
            if(request.readUint16()!=0x0700)throw std::runtime_error("Missing client-originated net-status request");
            const auto tick=request.readUint16BE();
            for(int n=0;n<5;++n)if(request.readUint32()!=0)throw std::runtime_error("Wrong reserved status fields");
            if(request.readUint32BE()!=1||request.readUint32BE()!=1)throw std::runtime_error("Wrong network-order packet counters");
            if(request.readByte()!=0)throw std::runtime_error("Wrong compression flag");
            soe::PacketBuffer reply;reply.writeUint16(0x0800);reply.writeUint16BE(tick);
            for(int n=0;n<9;++n)reply.writeUint32(0);
            reply.writeByte(0);reply.writeUint16(0);soe::XorCipher::encrypt(reply,0x12345678);
            // Exact engine3 response omits CRC generation. Duplicate must
            // not increase the response count or create application data.
            peer.send(reply);peer.send(reply);
            const auto data=peer.read();soe::PacketBuffer message(data.data(),data.size());
            soe::XorCipher::decrypt(message,0x12345678);
            if(message.readUint16()!=0x0900||message.readUint16BE()!=0)throw std::runtime_error("Keepalive consumed gameplay sequence");
            soe::PacketBuffer disconnect;disconnect.writeUint16(0x0500);disconnect.writeUint32(0);disconnect.writeUint16(0x0600);
            disconnect.writeByte(0);disconnect.writeUint16(0);soe::XorCipher::encrypt(disconnect,0x12345678);
            soe::PacketCrc::append(disconnect,0x12345678);
            auto bad=disconnect;bad.writeByteAt(bad.size()-1,bad.peekByte(bad.size()-1)^1);peer.send(bad);peer.send(disconnect);
        });
        asio::io_context io;soe::SoeSession client(io,"127.0.0.1",peer.port());client.connect(1s);
        const auto start=std::chrono::steady_clock::now();
        require(client.receiveMessages(7s).empty(),"Net-status response must not become gameplay data");
        const auto elapsed=std::chrono::steady_clock::now()-start;
        require(elapsed>=4900ms&&elapsed<6500ms,"Keepalive must fire during a long quiet receive at five seconds");
        require(client.transportStats().netStatusRequests==1&&client.transportStats().netStatusResponses==1,"Request and CRC-less echoed response must be counted");
        require(client.receiveMessages(500ms).empty(),"Duplicate control must be consumed");
        require(client.transportStats().netStatusResponses==1,"Duplicate response must not be accepted again");
        client.sendMessage({1,2,3});
        require(client.receiveMessages(500ms).empty(),"Bad-CRC disconnect must be rejected");
        bool disconnected=false;try{client.receiveMessages(500ms);}catch(const soe::DisconnectedError&){disconnected=true;}
        require(disconnected,"Valid-CRC disconnect must still end the session");
        peer.finish();
    }
    std::cout<<"{\"passed\":true,\"checks\":"<<checks<<",\"scope\":\"Loopback SOE lifecycle, five-second net-status during idle receive, CRC-less response, sequence isolation and disconnect CRC\"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
