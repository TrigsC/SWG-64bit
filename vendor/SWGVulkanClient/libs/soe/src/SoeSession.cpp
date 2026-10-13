#include "soe/SoeSession.h"

#include <iostream>
#include <algorithm>
#include <random>
#include <utility>

#include "soe/Compression.h"
#include "soe/Crc32.h"
#include "soe/PacketBuffer.h"
#include "soe/XorCipher.h"

namespace soe {

namespace {

// Splits `buf`'s remaining bytes into one or more message payloads: a
// BaseMultiPacket bundle (detected via the 0x1900 marker) becomes several,
// anything else is treated as a single message spanning the rest of the
// buffer. Shared between a normal (unfragmented) Data Channel payload and a
// freshly-reassembled Fragmented payload - Core3's own reference client
// re-runs this exact same check after reassembling a Fragmented packet (it
// hands the completed packet to the same handler an ordinary Data Channel
// packet goes through), so a large bundled message must be supported here
// too, not just for single-packet payloads.
void extractDataChannelMessages(PacketBuffer& buf, std::vector<std::vector<uint8_t>>& messages) {
    if (buf.remaining() >= 2 && buf.peekUint16(buf.readPosition()) == 0x1900) {
        buf.readUint16(); // consume the multi-packet marker
        while (buf.remaining() > 0) {
            uint8_t lenByte = buf.readByte();
            size_t subLen = (lenByte == 0xFF) ? buf.readUint16BE() : lenByte;
            if (subLen > buf.remaining()) {
                throw std::runtime_error("Truncated application bundle");
            }
            messages.push_back(buf.readBytes(subLen));
        }
    } else {
        messages.push_back(buf.readBytes(buf.remaining()));
    }
}

} // namespace

SoeSession::SoeSession(asio::io_context& io, const std::string& host, uint16_t port, ReliableChannel::Limits limits)
    : io_(io),
      workGuard_(asio::make_work_guard(io_)),
      socket_(io_, asio::ip::udp::endpoint(asio::ip::udp::v4(), 0)), reliable_(limits) {
    asio::ip::udp::resolver resolver(io_);
    auto results = resolver.resolve(asio::ip::udp::v4(), host, std::to_string(port));
    remoteEndpoint_ = *results.begin();
    socket_.connect(remoteEndpoint_);
}

SoeSession::~SoeSession() {
    if (connected_) {
        try {
            disconnect();
        } catch (const std::exception& e) {
            // Best-effort only - a destructor must never throw, and there's
            // nothing more we can do if the final Disconnect fails to send
            // (the socket is being torn down regardless). Still worth a
            // diagnostic: this is the safety net for a real, previously
            // confirmed stuck-session bug, so silently losing track of
            // whether it fired would make a recurrence indistinguishable
            // from this fix failing.
            std::cerr << "SoeSession: final Disconnect failed to send: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "SoeSession: final Disconnect failed to send (unknown error)\n";
        }
    }

    std::error_code ec;
    socket_.close(ec);
}

void SoeSession::sendRaw(const std::vector<uint8_t>& bytes) {
    socket_.send(asio::buffer(bytes));
    ++stats_.sent;
}

std::vector<uint8_t> SoeSession::receiveRawPacket(std::chrono::milliseconds timeout) {
    std::vector<uint8_t> buffer(2048);
    size_t bytesReceived = 0;
    bool timedOut = false;
    std::error_code recvError;
    bool recvDone = false;
    bool timerDone = false;

    asio::steady_timer timer(io_);
    timer.expires_after(timeout);

    socket_.async_receive(asio::buffer(buffer),
        [&](const std::error_code& ec, size_t bytes) {
            recvError = ec;
            bytesReceived = bytes;
            recvDone = true;
            timer.cancel();
        });

    timer.async_wait([&](const std::error_code& ec) {
        timerDone = true;
        if (!ec) {
            timedOut = true;
            socket_.cancel();
        }
    });

    // io_ is kept alive (and permanently "has work" via workGuard_) for the
    // whole session, so run_one() can be driven directly here: it blocks
    // until one ready handler runs, which we call repeatedly until the
    // receive itself completes (with real data, or with operation_aborted
    // from the timer's cancel() above) - no restart()/run() churn needed.
    // Retire both callbacks before their stack captures go out of scope.
    // A ready timeout must not cancel the following packet receive.
    while (!recvDone || !timerDone) {
        io_.run_one();
    }

    if (timedOut && bytesReceived == 0) {
        throw TimeoutError("SoeSession: timed out waiting for a packet");
    }
    if (recvError && recvError != asio::error::operation_aborted) {
        throw std::runtime_error("SoeSession: receive failed: " + recvError.message());
    }

    ++stats_.received;
    buffer.resize(bytesReceived);
    return buffer;
}

void SoeSession::connect(std::chrono::milliseconds timeout) {
    std::random_device rd;
    connectionId_ = rd();

    PacketBuffer request;
    request.writeUint16(0x0100); // SessionRequest opcode
    request.writeUint16(0x0000);
    request.writeUint16BE(2);    // crcLength
    request.writeUint32(connectionId_);
    request.writeUint32BE(496);  // maxRawSize (engine3 Packet::RAW_MAX_SIZE)

    sendRaw(std::vector<uint8_t>(request.data(), request.data() + request.size()));

    auto raw = receiveRawPacket(timeout);
    if (raw.size() != 17) {
        throw std::runtime_error(
            "SoeSession: unexpected SessionResponse size " + std::to_string(raw.size()));
    }

    PacketBuffer response(raw.data(), raw.size());
    uint16_t opcode = response.readUint16();
    if (opcode != 0x0200) {
        throw std::runtime_error(
            "SoeSession: expected SessionResponse (0x0200), got 0x" + std::to_string(opcode));
    }

    if(response.readUint32()!=connectionId_)
        throw std::runtime_error("SoeSession: SessionResponse connection ID mismatch");
    crcSeed_ = response.readUint32BE();
    // remaining 7 trailer bytes are a fixed constant, ignored.

    connected_ = true;
    lastValidated_ = std::chrono::steady_clock::now();
    reliable_.start(lastValidated_);
    nextNetStatus_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
}

void SoeSession::sendMessage(const std::vector<uint8_t>& payload) {
    if (!connected_) throw DisconnectedError("SoeSession: not connected");
    serviceTransport();
    if(payload.size()>65536)throw std::invalid_argument("Outbound message exceeds 64 KiB limit");
    std::vector<std::vector<uint8_t>> wires;
    const bool fragmented=payload.size()>489; // 496 minus opcode/sequence/flag/CRC
    size_t offset=0;
    auto sequence=reliable_.nextSendSequence();
    do {
        PacketBuffer buf;
        buf.writeUint16(fragmented?0x0D00:0x0900);buf.writeUint16BE(sequence++);
        if(fragmented&&offset==0)buf.writeUint32BE(static_cast<uint32_t>(payload.size()));
        const auto count=std::min(payload.size()-offset,size_t(493)-buf.size());
        if(count)buf.writeBytes(payload.data()+offset,count);offset+=count;
        buf.writeByte(0);buf.writeUint16(0);
        XorCipher::encrypt(buf,crcSeed_);PacketCrc::append(buf,crcSeed_);
        wires.emplace_back(buf.data(),buf.data()+buf.size());
    }while(offset<payload.size());
    reliable_.queue(std::move(wires));
    serviceTransport();
}

void SoeSession::disconnect() {
    if (!connected_) {
        return; // already disconnected (or never connected) - idempotent no-op
    }

    PacketBuffer buf;
    buf.writeUint16(0x0500); // Disconnect opcode
    buf.writeUint32(connectionId_);
    buf.writeUint16(0x0600); // fixed trailer constant, per DisconnectMessage
    buf.writeByte(0x00);     // compression flag
    buf.writeUint16(0);      // CRC placeholder

    // No sequence number: Core3's DisconnectMessage explicitly disables
    // sequencing for this packet (its own layout already uses the sequence
    // field's usual byte offset for connectionID).
    XorCipher::encrypt(buf, crcSeed_);
    PacketCrc::append(buf, crcSeed_);

    // Only mark ourselves disconnected once the send actually succeeds - if
    // sendRaw() throws, connected_ stays true so a later retry (including
    // the destructor's own best-effort attempt) still tries to notify the
    // server, instead of the failure silently disarming that safety net.
    sendRaw(std::vector<uint8_t>(buf.data(), buf.data() + buf.size()));
    connected_ = false;
}

void SoeSession::sendAck(uint16_t sequence) { sendSequenceControl(0x1500,sequence); }
void SoeSession::sendSequenceControl(uint16_t opcode,uint16_t sequence) {
    PacketBuffer buf;buf.writeUint16(opcode);buf.writeUint16BE(sequence);
    buf.writeByte(0);buf.writeUint16(0);
    XorCipher::encrypt(buf,crcSeed_);PacketCrc::append(buf,crcSeed_);
    sendRaw(std::vector<uint8_t>(buf.data(),buf.data()+buf.size()));
}
SoeSession::TransportStats SoeSession::transportStats() const {
    auto result=stats_;const auto& channel=reliable_.stats();
    result.retries=channel.retransmissions;result.acked=channel.acknowledged;
    result.duplicates=channel.duplicates;result.outOfOrder=channel.outOfOrder;result.rejected=channel.rejected;
    result.pendingPackets=reliable_.pending();result.bufferedPackets=reliable_.buffered();
    result.lastValidatedAgeMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-lastValidated_).count();
    return result;
}
[[noreturn]] void SoeSession::failConnection(const std::string& reason) {
    const auto detail=reason+" (pending="+std::to_string(reliable_.pending())+", buffered="+std::to_string(reliable_.buffered())+"); reconnect required";
    try { disconnect(); } catch (...) { connected_=false; }
    throw DisconnectedError("SoeSession: "+detail);
}
void SoeSession::serviceTransport() {
    if(!connected_)return;
    const auto now=std::chrono::steady_clock::now();
    try { reliable_.checkTimeouts(now); }
    catch(const std::exception& e) { failConnection(e.what()); }
    if(fragmentProgress_&&now-*fragmentProgress_>=reliable_.limits().deliveryTimeout)
        failConnection("Fragmented message stopped making progress");
    for(auto& transmission:reliable_.transmissions(now))sendRaw(transmission.wire);
    serviceNetStatus();
}

void SoeSession::serviceNetStatus() {
    const auto now = std::chrono::steady_clock::now();
    if (!connected_ || now < nextNetStatus_) return;
    const auto tick = static_cast<uint16_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    PacketBuffer request;
    request.writeUint16(0x0700);
    request.writeUint16BE(tick);
    // engine3 handleNetStatusRequest consumes five reserved LE words and
    // two network-order packet counters. This control is never sequenced.
    for (int i = 0; i < 5; ++i) request.writeUint32(0);
    request.writeUint32BE(stats_.sent);
    request.writeUint32BE(stats_.received);
    request.writeByte(0);
    request.writeUint16(0);
    XorCipher::encrypt(request, crcSeed_);
    PacketCrc::append(request, crcSeed_);
    sendRaw(std::vector<uint8_t>(request.data(), request.data() + request.size()));
    ++stats_.netStatusRequests;
    lastNetStatusTick_ = tick;
    awaitingNetStatus_ = true;
    nextNetStatus_ = now + std::chrono::seconds(5);
}

std::vector<std::vector<uint8_t>> SoeSession::receiveMessages(std::chrono::milliseconds timeout) {
    if(!connected_)throw DisconnectedError("SoeSession: not connected");
    const auto deadline=std::chrono::steady_clock::now()+timeout;
    std::vector<uint8_t> raw;
    for(;;) {
        serviceTransport();
        const auto now=std::chrono::steady_clock::now();
        auto wake=std::min({deadline,nextNetStatus_,reliable_.nextService(now)});
        if(fragmentProgress_)wake=std::min(wake,*fragmentProgress_+reliable_.limits().deliveryTimeout);
        try {raw=receiveRawPacket(std::max(std::chrono::ceil<std::chrono::milliseconds>(wake-now),std::chrono::milliseconds(0)));break;}
        catch(const TimeoutError&) {serviceTransport();if(std::chrono::steady_clock::now()>=deadline)throw;}
    }
    auto messages=processDatagram(raw);
    serviceTransport();
    return messages;
}

namespace {
// Outer CRC/decrypt/decompress is performed once. Child frames have no
// individual wire trailers. Validate all lengths before mutating windows.
void flattenSoe(const std::vector<uint8_t>& plain,std::vector<std::vector<uint8_t>>& frames,int depth=0) {
    if(depth>4||frames.size()>=256||plain.size()<2)throw std::runtime_error("SOE nesting/size limit");
    PacketBuffer p(plain.data(),plain.size());const auto opcode=p.readUint16();
    if(opcode==0x0300) {
        if(!p.remaining())throw std::runtime_error("Empty SOE multipacket");
        while(p.remaining()) {
            const auto length=p.readByte();
            if(length<2||length>p.remaining())throw std::runtime_error("Truncated SOE multipacket");
            flattenSoe(p.readBytes(length),frames,depth+1);
        }
    }else {
        if((opcode==0x0900||opcode==0x0D00)&&plain.size()<4)throw std::runtime_error("Truncated data frame");
        if((opcode==0x1500||opcode==0x1100)&&plain.size()!=4)throw std::runtime_error("Malformed sequence control");
        if(opcode==0x0500&&plain.size()!=8)throw std::runtime_error("Malformed disconnect");
        frames.push_back(plain);
    }
}
}
std::vector<std::vector<uint8_t>> SoeSession::processDatagram(const std::vector<uint8_t>& raw) {
    PacketBuffer buf(raw.data(),raw.size());
    if(raw.size()<2||raw.size()>496){++stats_.invalidPackets;return {};}
    const auto opcode=buf.peekUint16(0);
    const auto now=std::chrono::steady_clock::now();
    if(opcode==0x0800&&raw.size()==43&&awaitingNetStatus_) {
        XorCipher::decrypt(buf,crcSeed_);
        bool valid=buf.peekUint16BE(2)==lastNetStatusTick_;
        for(size_t i=4;i<buf.size();++i)valid=valid&&buf.peekByte(i)==0;
        if(valid){++stats_.netStatusResponses;awaitingNetStatus_=false;lastValidated_=now;reliable_.notePeer(now);}
        else ++stats_.invalidPackets;
        return {};
    }
    if(raw.size()<5||!PacketCrc::test(buf,crcSeed_)){++stats_.invalidPackets;return {};}
    std::vector<std::vector<uint8_t>> frames;
    try {
        XorCipher::decrypt(buf,crcSeed_);
        const auto flag=buf.peekByte(buf.size()-3);
        if(flag>1)throw std::runtime_error("Unsupported compression flag");
        if(flag==1) {
            auto inflated=Compression::decompress(buf.data()+2,buf.size()-5,8192);
            PacketBuffer rebuilt;rebuilt.writeUint16(opcode);rebuilt.writeBytes(inflated);
            flattenSoe({rebuilt.data(),rebuilt.data()+rebuilt.size()},frames);
        }else {
            buf.removeLastBytes(3);flattenSoe({buf.data(),buf.data()+buf.size()},frames);
        }
    }catch(const std::exception& e){++stats_.invalidPackets;std::cerr<<"SoeSession: dropping malformed packet: "<<e.what()<<"\n";return {};}
    std::vector<std::vector<uint8_t>> messages;
    for(auto& frame:frames) {
        PacketBuffer p(frame.data(),frame.size());const auto op=p.readUint16();
        if(op!=0x0900&&op!=0x0D00&&op!=0x1500&&op!=0x1100&&op!=0x0500&&op!=0x0600)continue;
        lastValidated_=now;reliable_.notePeer(now);
        if(op==0x0500){connected_=false;throw DisconnectedError("SoeSession: peer sent Disconnect");}
        if(op==0x1500){reliable_.acknowledge(p.readUint16BE());continue;}
        // engine3's selective out-of-order resend handler is disabled at
        // the pinned revision. Our timer retries pending data; an advisory
        // hint must not acknowledge or free any outbound packet.
        if(op==0x1100||op==0x0600)continue;
        auto delivery=reliable_.receive({op,p.readUint16BE(),p.readBytes(p.remaining())},now);
        if(delivery.gap)sendSequenceControl(0x1100,*delivery.gap);
        if(delivery.ack)sendAck(*delivery.ack);
        for(auto& ordered:delivery.frames) {
            try {
                PacketBuffer data(ordered.payload.data(),ordered.payload.size());
                std::vector<std::vector<uint8_t>> decoded;
                if(ordered.opcode==0x0D00) {
                    auto fragment=fragmentReassembler_.addFragment(ordered.sequence,data);
                    if(fragment.status==FragmentStatus::Invalid)throw std::runtime_error(fragment.error);
                    if(fragment.status==FragmentStatus::Incomplete){fragmentProgress_=now;continue;}
                    fragmentProgress_.reset();PacketBuffer full(fragment.payload.data(),fragment.payload.size());extractDataChannelMessages(full,decoded);
                }else extractDataChannelMessages(data,decoded);
                messages.insert(messages.end(),std::make_move_iterator(decoded.begin()),std::make_move_iterator(decoded.end()));
            }catch(const std::exception& e){++stats_.invalidPackets;if(!fragmentReassembler_.inProgress())fragmentProgress_.reset();std::cerr<<"SoeSession: dropping malformed application frame: "<<e.what()<<"\n";}
        }
    }
    return messages;
}

} // namespace soe
