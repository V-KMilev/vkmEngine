#include "net/net_support.h"

#include <thread>

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
    check(
        "  whole, bytes and all",
        heard.size() == sizeof(hello) && std::memcmp(heard.data(), hello, sizeof(hello)) == 0
    );
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
    check(
        "a datagram too big to survive the path is refused",
        !client.send(server.localAddress(), huge.data(), huge.size())
    );

    const std::vector<uint8_t> biggest(UdpSocket::MAX_DATAGRAM, 0xCD);
    check(
        "  and the largest that fits is taken",
        client.send(server.localAddress(), biggest.data(), biggest.size())
    );
}

void testAnEmptyDatagramIsOneReadOfItsOwn() {
    std::printf("A datagram of no bytes, behind which a real one is waiting:\n");

    UdpSocket server, client;
    check("two sockets open", server.open(0) && client.open(0));

    // Anyone can send one of these. A host bounds how many datagrams it reads
    // a frame, so each has to cost one read: stepped over inside the socket,
    // a flood of them would be read without end and without being counted.
    sockaddr_in to{};
    to.sin_family      = AF_INET;
    to.sin_addr.s_addr = htonl(0x7F000001u);
    to.sin_port        = htons(server.localAddress().port);

    const RawSocket raw = rawSocketUdp();
    check("a raw sender opens", rawSocketOpen(raw));
    check(
        "an empty datagram is sent",
        ::sendto(raw, nullptr, 0, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == 0
    );

    const uint8_t behind[] = {'r', 'e', 'a', 'l'};
    const auto sent = ::sendto(
        raw,
        reinterpret_cast<const char*>(behind),
        sizeof(behind),
        0,
        reinterpret_cast<sockaddr*>(&to),
        sizeof(to)
    );
    check("  and a real one behind it", sent == sizeof(behind));

    // Loopback is quick but not instant; both must be queued before the reads
    // below, or this would pass by reading them on separate passes.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<uint8_t> heard = {'x'};
    NetAddress from;
    check("the empty datagram is a read", server.receive(heard, from));
    check("  holding nothing", heard.empty());

    int delivered = 0;
    std::vector<uint8_t> last;
    while (server.receive(heard, from)) {
        ++delivered;
        last = heard;
    }
    check("and the real one is the next read", delivered == 1);
    check("  whole", last.size() == sizeof(behind) && std::memcmp(last.data(), behind, sizeof(behind)) == 0);

    rawSocketClose(raw);
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
    check(
        "a framed packet carries its payload after the header",
        wire.size() == NetConnection::HEADER_BYTES + sizeof(hello)
    );
    check("and bob takes it", bob.accept(wire.data(), wire.size(), got, acknowledged));
    check("with the payload intact", got.size() == 3 && got[0] == 7);

    // The same datagram arriving twice - a duplicate, or a retransmit by some
    // middlebox. Applying it again would be applying a moment already applied.
    check(
        "the same packet twice is refused the second time",
        !bob.accept(wire.data(), wire.size(), got, acknowledged)
    );

    // Out of order. Alice sends three, bob gets the third before the second.
    std::vector<uint8_t> second, third;
    alice.frame(hello, sizeof(hello), second);
    alice.frame(hello, sizeof(hello), third);
    check("the newer of two arrives", bob.accept(third.data(), third.size(), got, acknowledged));
    check(
        "and the older behind it is refused, not applied backwards",
        !bob.accept(second.data(), second.size(), got, acknowledged)
    );

    // Bob answers, and his answer carries what he has seen. Alice reads her own
    // round trip out of it without either end sending a ping.
    alice.advance(0.030f);
    bob.frame(nullptr, 0, wire);
    check("alice takes bob's answer", alice.accept(wire.data(), wire.size(), got, acknowledged));
    check("an empty payload is a legal packet", got.empty());
    check(
        "and alice now knows the trip took about thirty milliseconds",
        std::abs(alice.roundTrip() - 0.030f) < 0.001f
    );

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

// The acknowledgement window is the newest sequence and the thirty-two before
// it. A packet exactly thirty-two behind the one that just landed is the
// window's last bit, not outside it: dropped there, a snapshot that arrived is
// never confirmed and is said again for nothing.
void testThePacketThirtyTwoBackIsStillAcknowledged() {
    std::printf("A packet that lands thirty-two after the last one that did:\n");

    NetConnection sender, receiver;
    sender.open(NetAddress{0x7F000001u, 40021});
    receiver.open(NetAddress{0x7F000001u, 40022});

    std::vector<uint8_t> wire, got;
    std::vector<uint16_t> acknowledged;

    const uint16_t first = sender.nextSequence();
    sender.frame(nullptr, 0, wire);
    check("the first arrives", receiver.accept(wire.data(), wire.size(), got, acknowledged));

    // Thirty-one lost, then the thirty-second arrives.
    for (int i = 0; i < 31; ++i) sender.frame(nullptr, 0, wire);
    const uint16_t last = sender.nextSequence();
    sender.frame(nullptr, 0, wire);
    check("the one thirty-two on arrives", receiver.accept(wire.data(), wire.size(), got, acknowledged));

    receiver.frame(nullptr, 0, wire);
    acknowledged.clear();
    check("the sender takes the answer", sender.accept(wire.data(), wire.size(), got, acknowledged));
    const auto confirmed = [&](uint16_t sequence) {
        return std::find(acknowledged.begin(), acknowledged.end(), sequence) != acknowledged.end();
    };
    check("  which confirms the newest", confirmed(last));
    check("  and the one thirty-two before it", confirmed(first));
    check("  and none of the thirty-one lost between", acknowledged.size() == 2);
}

// A join token is a MAC of the address that asked: what a server hands one
// address must be worth nothing from another, and nothing once it is old.
void testAJoinTokenProvesOneAddressForAWhile() {
    std::printf("What a join token proves, and for how long:\n");

    // The reference vectors from the SipHash paper: key 00..0f, and messages
    // of no bytes and of the fifteen bytes 00..0e.
    const uint64_t k0 = 0x0706050403020100ull;
    const uint64_t k1 = 0x0f0e0d0c0b0a0908ull;
    uint8_t fifteen[15];
    for (uint8_t i = 0; i < 15; ++i) fifteen[i] = i;
    check("SipHash-2-4 of nothing is the paper's", sipHash24(k0, k1, nullptr, 0) == 0x726fdb47dd0e0e31ull);
    check("  and of fifteen bytes", sipHash24(k0, k1, fifteen, sizeof(fifteen)) == 0xa129ca6149be45e5ull);

    NetJoinCookie cookie;
    cookie.rekey();
    const NetAddress player{0x7F000001u, 40031};
    const NetAddress other{0x7F000001u, 40032};
    const NetAddress elsewhere{0x0A000004u, 40031};

    const double now = 1234.5;
    const uint64_t token = cookie.issue(player, now);
    check("a token is taken back from the address it was given to", cookie.accepts(player, token, now));
    check("  not from another port", !cookie.accepts(other, token, now));
    check("  nor another host", !cookie.accepts(elsewhere, token, now));
    check("  and two addresses are given different tokens", cookie.issue(other, now) != token);
    check(
        "it is still good in the next window",
        cookie.accepts(player, token, now + NetJoinCookie::WINDOW_SECONDS)
    );
    check(
        "  and not in the one after",
        !cookie.accepts(player, token, now + 2.0 * NetJoinCookie::WINDOW_SECONDS)
    );

    NetJoinCookie restarted;
    restarted.rekey();
    check(
        "a server with a key of its own gives out other tokens",
        restarted.issue(player, now) != token && !restarted.accepts(player, token, now)
    );
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
    check("the server takes a first hello", server.accept(wire.data(), wire.size(), payload, acknowledged));

    // The Welcome. Its sequence is what the server later reads as proof the
    // address can receive, so it has to be a packet, not a default.
    const uint16_t welcomeSequence = server.nextSequence();
    check(
        "no packet is numbered the value that means nothing",
        welcomeSequence != NetConnection::NO_SEQUENCE
    );

    const uint8_t welcome[] = {9};
    std::vector<uint8_t> out;
    server.frame(welcome, sizeof(welcome), out);

    // Lost. The client says hello again - it still has heard nothing at all,
    // and every header it sends has to put something in the acknowledgement
    // field regardless.
    client.frame(hello, sizeof(hello), wire);
    acknowledged.clear();
    check(
        "the server takes the second hello",
        server.accept(wire.data(), wire.size(), payload, acknowledged)
    );

    check("a peer that has received nothing acknowledges nothing", acknowledged.empty());
    check(
        "  so the packet proving it can receive stays unconfirmed",
        std::find(acknowledged.begin(), acknowledged.end(), welcomeSequence) == acknowledged.end()
    );
    check("  and no round trip is invented from it", server.roundTrip() == 0.0f);
}

} // namespace

void runNetTransportTests() {
    testTwoSocketsCanTalk();
    testAnEmptyDatagramIsOneReadOfItsOwn();
    testAConversationKnowsWhatArrived();
    testSequencesSurviveTheirOwnWrap();
    testThePacketThirtyTwoBackIsStillAcknowledged();
    testAJoinTokenProvesOneAddressForAWhile();
    testAPeerThatHasHeardNothingAcknowledgesNothing();
}
