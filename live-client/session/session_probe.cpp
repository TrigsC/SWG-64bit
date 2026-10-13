// Credential-free protocol diagnostic: no login, character, or gameplay data.
#include "soe/SoeSession.h"
#include "soe/PacketBuffer.h"
#include "soe/Crc32.h"
#include <iostream>
#include <thread>
using namespace std::chrono_literals;
int main(int argc,char**argv) {
    if(argc!=4) {std::cerr<<"Usage: native-session-probe HOST PORT baseline|maintained\n";return 2;}
    try {
        asio::io_context io;
        const std::string mode=argv[3];
        const auto port=std::stoul(argv[2]);
        if(port==0||port>65535||(mode!="baseline"&&mode!="maintained"))throw std::runtime_error("Invalid probe options");
        auto start=std::chrono::steady_clock::now();
        bool disconnected=false;soe::SoeSession::TransportStats stats;
        if(mode=="maintained") {
            soe::SoeSession session(io,argv[1],uint16_t(port));session.connect(5s);start=std::chrono::steady_clock::now();
            while(std::chrono::steady_clock::now()-start<65s) {
                try {session.receiveMessages(100ms);}
                catch(const soe::TimeoutError&) {}
                catch(const soe::DisconnectedError&) {disconnected=true;break;}
            }
            stats=session.transportStats();session.disconnect();
        } else {
            // Reproduce the old idle transport without sending keepalives.
            // Connected UDP restricts received datagrams to this endpoint.
            asio::ip::udp::resolver resolver(io);asio::ip::udp::socket socket(io,asio::ip::udp::v4());
            socket.connect(*resolver.resolve(asio::ip::udp::v4(),argv[1],argv[2]).begin());socket.non_blocking(true);
            soe::PacketBuffer request;request.writeUint16(0x0100);request.writeUint16(0);request.writeUint16BE(2);
            const auto id=uint32_t(std::chrono::steady_clock::now().time_since_epoch().count());
            request.writeUint32(id);request.writeUint32BE(496);socket.send(asio::buffer(request.data(),request.size()));
            uint32_t seed=0;bool connected=false;
            while(std::chrono::steady_clock::now()-start<65s) {
                uint8_t raw[2048];std::error_code ec;const auto size=socket.receive(asio::buffer(raw),0,ec);
                if(ec==asio::error::would_block||ec==asio::error::try_again) {
                    if(!connected&&std::chrono::steady_clock::now()-start>5s)throw std::runtime_error("Handshake timed out");
                    std::this_thread::sleep_for(10ms);continue;
                }
                if(ec)throw std::runtime_error(ec.message());
                soe::PacketBuffer packet(raw,size);
                if(!connected) {
                    if(size!=17||packet.readUint16()!=0x0200||packet.readUint32()!=id)throw std::runtime_error("Invalid handshake");
                    seed=packet.readUint32BE();connected=true;start=std::chrono::steady_clock::now();
                } else if(size>=2&&packet.peekUint16(0)==0x0500&&soe::PacketCrc::test(packet,seed)) {disconnected=true;break;}
            }
        }
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
        std::cout<<"{\"scope\":\"credential-free SOE transport only\",\"mode\":\""<<mode<<"\",\"elapsed_ms\":"<<elapsed
                 <<",\"peer_disconnected\":"<<(disconnected?"true":"false")<<",\"net_status_requests\":"<<stats.netStatusRequests
                 <<",\"net_status_responses\":"<<stats.netStatusResponses<<"}\n";
        return 0;
    } catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
