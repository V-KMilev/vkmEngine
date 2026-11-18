#include "net/net_core.h"

namespace Vkm::Engine {

void sendBare(NetCore& core, Network::Connection& connection, NetMessage message, uint64_t token) {
    Network::BitWriter writer(core.payload, 16);
    writeMessageHeader(writer, message, token);
    writer.finish();
    connection.frame(core.payload.data(), core.payload.size(), core.datagram);
    core.socket.send(connection.peer(), core.datagram.data(), core.datagram.size());
}

Network::BitReader receivedPayload(const NetCore& core) {
    const size_t header = Network::Connection::HEADER_BYTES;
    if (core.datagram.size() < header) return Network::BitReader(core.datagram.data(), 0);
    return Network::BitReader(core.datagram.data() + header, core.datagram.size() - header);
}

} // namespace Vkm::Engine
