#include "net/wire/protocol.h"

namespace Vkm::Engine {

const char* toString(NetRefusal reason) {
    switch (reason) {
        case NetRefusal::Full:     return "the server is full";
        case NetRefusal::Mismatch: return "a different build or world";
        case NetRefusal::Declined: return "the game declined the connection";
        case NetRefusal::TickRate: return "the server ticks at a different rate";
        default:                   return "no reason given";
    }
}

void writeMessageHeader(BitWriter& out, NetMessage message, uint64_t token) {
    out.u16(NET_PROTOCOL_TAG);
    out.u8(NET_PROTOCOL_VERSION);
    out.u8(static_cast<uint8_t>(message));
    out.u64(token);
}

NetSender readMessageHeader(BitReader& in, NetMessage& message, uint64_t& token) {
    if (in.u16() != NET_PROTOCOL_TAG) return NetSender::Unknown;
    const uint8_t version = in.u8();
    const uint8_t raw     = in.u8();
    token = in.u64();
    if (in.failed() || raw == 0) return NetSender::Unknown;

    if (version != NET_PROTOCOL_VERSION) {
        const bool understood = raw == static_cast<uint8_t>(NetMessage::Hello)
            || raw == static_cast<uint8_t>(NetMessage::Refuse);
        if (!understood) return NetSender::Unknown;
        message = static_cast<NetMessage>(raw);
        return NetSender::OtherBuild;
    }
    if (raw >= static_cast<uint8_t>(NetMessage::Count)) return NetSender::Unknown;
    message = static_cast<NetMessage>(raw);
    return NetSender::ThisBuild;
}

} // namespace Vkm::Engine
