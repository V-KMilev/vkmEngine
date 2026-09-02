#include "support.h"

namespace {

void testTwoSocketsCanTalk() {
    std::printf("Two UDP sockets on one machine:\n");

    UdpSocket server, client;
    check("a socket takes a port the system picks", server.open(0));
    check("  and reports which one it got", server.localAddress().port != 0);
    check("a second one opens beside it", client.open(0));
    check("  on a different port", client.localAddress().port != server.localAddress().port);

    std::vector<uint8_t> heard;
    NetAddress from;
    check("an empty socket says so rather than waiting", !server.receive(heard, from));
    check("  and leaves nothing behind to be read as a packet", heard.empty());

    const uint8_t hello[] = {'h', 'i', 0, 200, 255};
    check("a datagram goes out", client.send(server.localAddress(), hello, sizeof(hello)));

    // Loopback is not instant and is not ordered by the clock, so this waits
    // rather than assuming the next read has it.
    bool arrived = false;
    for (int i = 0; i < 200 && !arrived; ++i) {
        arrived = server.receive(heard, from);
        if (!arrived) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    check("  and arrives", arrived);
    check("  whole, bytes and all",
          heard.size() == sizeof(hello) && std::memcmp(heard.data(), hello, sizeof(hello)) == 0);
    check("  saying which port it came from", from.port == client.localAddress().port);

    // The reply proves the sender's address is usable, not merely reported.
    const uint8_t back[] = {'o', 'k'};
    check("and the answer goes back the way it came", server.send(from, back, sizeof(back)));

    std::vector<uint8_t> echo;
    NetAddress replyFrom;
    bool returned = false;
    for (int i = 0; i < 200 && !returned; ++i) {
        returned = client.receive(echo, replyFrom);
        if (!returned) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check("  and lands", returned && echo.size() == 2 && echo[0] == 'o');

    // What must never be sent: past this it is fragmented, and one lost
    // fragment loses the whole thing.
    const std::vector<uint8_t> huge(UdpSocket::MAX_DATAGRAM + 1, 0xAB);
    check("a datagram too big to survive the path is refused",
          !client.send(server.localAddress(), huge.data(), huge.size()));

    const std::vector<uint8_t> biggest(UdpSocket::MAX_DATAGRAM, 0xCD);
    check("  and the largest that fits is taken",
          client.send(server.localAddress(), biggest.data(), biggest.size()));
}

void testAnEmptyDatagramDoesNotEndTheDrain() {
    std::printf("A datagram of no bytes, behind which a real one is waiting:\n");

    UdpSocket server, client;
    check("two sockets open", server.open(0) && client.open(0));

    // Anyone can send one of these, and a host reads its socket until it says
    // empty. If nothing separates "a packet of no bytes" from "no packet", the
    // first one sent costs a host the rest of that frame's traffic.
    sockaddr_in to{};
    to.sin_family      = AF_INET;
    to.sin_addr.s_addr = htonl(0x7F000001u);
    to.sin_port        = htons(server.localAddress().port);

    const auto raw = ::socket(AF_INET, SOCK_DGRAM, 0);
    check("a raw sender opens", raw >= 0);
    check("an empty datagram is sent",
          ::sendto(raw, nullptr, 0, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == 0);

    const uint8_t behind[] = {'r', 'e', 'a', 'l'};
    check("  and a real one behind it",
          ::sendto(raw, reinterpret_cast<const char*>(behind), sizeof(behind), 0,
                   reinterpret_cast<sockaddr*>(&to), sizeof(to)) == sizeof(behind));

    // Loopback is quick but not instant; both must be queued before the drain
    // below, or this would pass by reading them on separate passes.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // One frame's worth: read until the socket says there is nothing more.
    std::vector<uint8_t> heard;
    NetAddress from;
    int delivered = 0;
    std::vector<uint8_t> last;
    while (server.receive(heard, from)) {
        ++delivered;
        last = heard;
    }

    check("the real datagram still comes out of that one drain", delivered == 1);
    check("  whole", last.size() == sizeof(behind)
                  && std::memcmp(last.data(), behind, sizeof(behind)) == 0);

#if defined(_WIN32)
    ::closesocket(raw);
#else
    ::close(raw);
#endif
}

void testAConversationKnowsWhatArrived() {
    std::printf("What each end learns from traffic it was sending anyway:\n");

    NetConnection alice, bob;
    alice.open(NetAddress{0x7F000001u, 40001});
    bob.open(NetAddress{0x7F000001u, 40002});

    std::vector<uint8_t> wire, got;
    std::vector<uint16_t> acknowledged;
    const uint8_t hello[] = {7, 7, 7};

    alice.frame(hello, sizeof(hello), wire);
    check("a framed packet carries its payload after the header",
          wire.size() == NetConnection::HEADER_BYTES + sizeof(hello));
    check("and bob takes it", bob.accept(wire.data(), wire.size(), got, acknowledged));
    check("with the payload intact", got.size() == 3 && got[0] == 7);

    // The same datagram arriving twice - a duplicate, or a retransmit by some
    // middlebox. Applying it again would be applying a moment already applied.
    check("the same packet twice is refused the second time",
          !bob.accept(wire.data(), wire.size(), got, acknowledged));

    // Out of order. Alice sends three, bob gets the third before the second.
    std::vector<uint8_t> second, third;
    alice.frame(hello, sizeof(hello), second);
    alice.frame(hello, sizeof(hello), third);
    check("the newer of two arrives", bob.accept(third.data(), third.size(), got, acknowledged));
    check("and the older behind it is refused, not applied backwards",
          !bob.accept(second.data(), second.size(), got, acknowledged));

    // Bob answers, and his answer carries what he has seen. Alice reads her own
    // round trip out of it without either end sending a ping.
    alice.advance(0.030f);
    bob.frame(nullptr, 0, wire);
    check("alice takes bob's answer", alice.accept(wire.data(), wire.size(), got, acknowledged));
    check("an empty payload is a legal packet", got.empty());
    check("and alice now knows the trip took about thirty milliseconds",
          std::abs(alice.roundTrip() - 0.030f) < 0.001f);

    // Silence. Neither timer is about the game's clock - they are wall time, so
    // a peer that stops answering is noticed even while the world is paused.
    check("a fresh connection is not timed out", !bob.timedOut());
    bob.advance(NetConnection::TIMEOUT_SECONDS + 0.1f);
    check("but one that has heard nothing for the timeout is", bob.timedOut());

}

void testSequencesSurviveTheirOwnWrap() {
    std::printf("Sixteen bits run out after about eighteen minutes at sixty a second:\n");

    // The interesting packet is not the first, it is the one after 65535. If
    // newer-than is a plain comparison the connection stops accepting anything
    // at that point and the game dies at a fixed time after it started.
    NetConnection sender, receiver;
    sender.open(NetAddress{0x7F000001u, 1});
    receiver.open(NetAddress{0x7F000001u, 2});

    std::vector<uint8_t> wire, got;
    std::vector<uint16_t> acknowledged;
    size_t accepted = 0;
    for (int i = 0; i < 70000; ++i) {
        sender.frame(nullptr, 0, wire);
        if (receiver.accept(wire.data(), wire.size(), got, acknowledged)) ++accepted;
    }
    check("every packet across the wrap is accepted", accepted == 70000);
    std::printf("      %zu packets, sequence wrapped %d time(s)\n", accepted, 70000 / 65536);
}

// The join as NetSession actually plays it: greet() opens a peer connection and
// welcome() stamps its first sequence, so the one packet a never-heard peer
// could falsely acknowledge is exactly the one that proves it can receive.
void testAPeerThatHasHeardNothingAcknowledgesNothing() {
    std::printf("What a connection claims before it has heard anything:\n");

    NetConnection server, client;
    server.open(NetAddress{0x7F000001u, 40011});
    client.open(NetAddress{0x7F000001u, 40012});

    std::vector<uint8_t> wire, payload;
    std::vector<uint16_t> acknowledged;
    const uint8_t hello[] = {1, 2, 3};

    client.frame(hello, sizeof(hello), wire);
    check("the server takes a first hello",
          server.accept(wire.data(), wire.size(), payload, acknowledged));

    // The Welcome. Its sequence is what the server later reads as proof the
    // address can receive, so it has to be a packet, not a default.
    const uint16_t welcomeSequence = server.nextSequence();
    check("no packet is numbered the value that means nothing",
          welcomeSequence != NetConnection::NO_SEQUENCE);

    const uint8_t welcome[] = {9};
    std::vector<uint8_t> out;
    server.frame(welcome, sizeof(welcome), out);

    // Lost. The client says hello again - it still has heard nothing at all,
    // and every header it sends has to put something in the acknowledgement
    // field regardless.
    client.frame(hello, sizeof(hello), wire);
    acknowledged.clear();
    check("the server takes the second hello",
          server.accept(wire.data(), wire.size(), payload, acknowledged));

    check("a peer that has received nothing acknowledges nothing", acknowledged.empty());
    check("  so the packet proving it can receive stays unconfirmed",
          std::find(acknowledged.begin(), acknowledged.end(), welcomeSequence)
              == acknowledged.end());
    check("  and no round trip is invented from it", server.roundTrip() == 0.0f);
}

void testAMessageThatMustArriveDoes() {
    std::printf("The few things no later packet would say again:\n");

    NetReliable sender, receiver;
    const auto message = [](const char* text) {
        return std::vector<uint8_t>(text, text + std::strlen(text));
    };

    const std::vector<uint8_t> one   = message("spawn:crate");
    const std::vector<uint8_t> two   = message("spawn:barrel");
    const std::vector<uint8_t> three = message("despawn:crate");
    check("a message is accepted", sender.queue(one.data(), one.size()));
    check("and another", sender.queue(two.data(), two.size()));
    check("and a third", sender.queue(three.data(), three.size()));
    check("all three are waiting", sender.pending() == 3);

    // Every packet the sender writes carries the oldest unconfirmed messages.
    // Losing packets costs nothing but time.
    std::vector<uint8_t> packet;
    std::vector<std::vector<uint8_t>> got;
    const auto exchange = [&](bool deliver, bool reply) {
        packet.clear();
        BitWriter writer(packet, 1024);
        sender.write(writer);
        writer.finish();
        if (!deliver) return;

        BitReader reader(packet.data(), packet.size());
        check("the block decodes", receiver.read(reader, got));

        if (!reply) return;
        std::vector<uint8_t> back;
        BitWriter backWriter(back, 1024);
        receiver.write(backWriter);
        backWriter.finish();
        std::vector<std::vector<uint8_t>> none;
        BitReader backReader(back.data(), back.size());
        sender.read(backReader, none);
        check("nothing comes back the other way", none.empty());
    };

    // Ten packets lost outright before one gets through.
    for (int i = 0; i < 10; ++i) exchange(false, false);
    check("nothing was delivered while every packet was lost", got.empty());
    check("and the sender still holds all of them", sender.pending() == 3);

    exchange(true, false);
    check("one packet arriving delivers all three", got.size() == 3);
    check("in the order they were made",
          got[0] == one && got[1] == two && got[2] == three);

    // The sender has not heard yet, so it keeps sending them - and the receiver
    // must not deliver them a second time.
    exchange(true, false);
    check("a repeat is not delivered twice", got.size() == 3);
    check("and the sender is still holding them", sender.pending() == 3);

    // Now the acknowledgement gets back.
    exchange(true, true);
    check("once confirmed, the sender lets them go", sender.pending() == 0);
    check("and still delivered each exactly once", got.size() == 3);

    // A message queued afterwards flows on the same channel.
    const std::vector<uint8_t> four = message("spawn:ramp");
    sender.queue(four.data(), four.size());
    exchange(true, true);
    check("a later message arrives too", got.size() == 4 && got[3] == four);
    check("and is confirmed", sender.pending() == 0);
}

void testAMessageBlockRefusesWhatItCannotCarry() {
    std::printf("What the channel will not do:\n");

    NetReliable channel;
    const std::vector<uint8_t> huge(NetReliable::MAX_MESSAGE + 1, 0x7Fu);
    check("a message too big for a packet is refused, not truncated",
          !channel.queue(huge.data(), huge.size()));
    check("and an empty one is refused too", !channel.queue(huge.data(), 0));
    check("neither was queued", channel.pending() == 0);

    // A peer that never confirms must not grow the queue without limit.
    const std::vector<uint8_t> small(8, 1u);
    size_t accepted = 0;
    for (size_t i = 0; i < NetReliable::MAX_QUEUED + 8; ++i) {
        if (channel.queue(small.data(), small.size())) ++accepted;
    }
    check("a peer that stops confirming fills the queue and no more",
          accepted == NetReliable::MAX_QUEUED);
    check("which is a connection to give up on, and says so",
          channel.pending() == NetReliable::MAX_QUEUED);

    // More than one packet's worth: the block carries what fits and the rest
    // waits, still in order.
    NetReliable big, far;
    const std::vector<uint8_t> chunk(NetReliable::MAX_MESSAGE, 0x2Au);
    for (int i = 0; i < 6; ++i) big.queue(chunk.data(), chunk.size());

    std::vector<uint8_t> packet;
    BitWriter writer(packet, 4096);
    big.write(writer);
    writer.finish();
    check("one packet's block stays inside its reservation",
          packet.size() <= NetReliable::BUDGET_BYTES + 32);

    std::vector<std::vector<uint8_t>> got;
    BitReader reader(packet.data(), packet.size());
    check("and what it carried decodes", far.read(reader, got));
    check("carrying fewer than were queued", got.size() < 6 && !got.empty());
    std::printf("      %zu of 6 messages of %zu bytes fit one packet\n",
                got.size(), NetReliable::MAX_MESSAGE);
}

} // namespace

void runNetTransportTests() {
    testTwoSocketsCanTalk();
    testAnEmptyDatagramDoesNotEndTheDrain();
    testAConversationKnowsWhatArrived();
    testSequencesSurviveTheirOwnWrap();
    testAPeerThatHasHeardNothingAcknowledgesNothing();
    testAMessageThatMustArriveDoes();
    testAMessageBlockRefusesWhatItCannotCarry();
}
