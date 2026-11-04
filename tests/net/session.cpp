// A server and its clients in one process, in lockstep: joining, snapshots, prediction, the link's
// faults and the handshake's refusals.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/clock.h"
#include "ecs/component/net/net_local.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/prefab_instances.h"
#include "io/scene/prefab.h"
#include "net/net_events.h"
#include "net/replication/silence.h"
#include "net/wire/protocol.h"
#include "net/wire/schema.h"

#include "harness/check.h"
#include "harness/lockstep.h"
#include "net/match.h"

using namespace Vkm::Test;
using namespace Vkm::Network;
using namespace Vkm::Platform;

namespace {

/// The entity that holds a CharacterController in @p scene: a player's character.
EntityId characterIn(Scene& scene) {
    EntityId found;
    scene.forEach<CharacterController>([&](EntityId id, CharacterController&) { if (!found) found = id; });
    return found;
}

int childrenOf(Scene& scene, EntityId parent) {
    int count = 0;
    HierarchyOperations::forEachChild(scene, parent, [&](EntityId) { ++count; });
    return count;
}

/// The client's copy of the server's entity @p id: the same slot, while the client's own fill no
/// slot of the server's.
EntityId sameSlot(Scene& scene, EntityId id) { return scene.entityAt(id.slot()); }

/**
 * @brief Make @p datagram a Mismatch refusal carrying @p token, as a server at @p version of the wire
 *        writes one.
 *
 * @param datagram A datagram to the client; its connection header is kept.
 * @param token    The token the refusal carries.
 * @param version  The version of the wire it is written at.
 */
void makeRefusal(std::vector<uint8_t>& datagram, uint64_t token, int version) {
    constexpr size_t VERSION = 10;
    std::vector<uint8_t> payload;
    BitWriter writer(payload, 32);
    writeMessageHeader(writer, NetMessage::Refuse, token);
    writer.u8(static_cast<uint8_t>(NetRefusal::Mismatch));
    writer.finish();
    datagram.resize(Connection::HEADER_BYTES);
    datagram.insert(datagram.end(), payload.begin(), payload.end());
    datagram[VERSION] = static_cast<uint8_t>(version);
}

/**
 * @brief Make @p datagram a Challenge giving @p token and echoing @p echo, as a server at @p version of
 *        the wire writes one.
 *
 * @param datagram A datagram to the client; its connection header is kept.
 * @param token    The token the client is to say Hello with.
 * @param echo     The token of the Hello it answers.
 * @param version  The version of the wire it is written at.
 */
void makeChallenge(std::vector<uint8_t>& datagram, uint64_t token, uint64_t echo, int version) {
    constexpr size_t VERSION = 10;
    std::vector<uint8_t> payload;
    BitWriter writer(payload, 32);
    writeMessageHeader(writer, NetMessage::Challenge, token);
    writer.u64(echo);
    writer.finish();
    datagram.resize(Connection::HEADER_BYTES);
    datagram.insert(datagram.end(), payload.begin(), payload.end());
    datagram[VERSION] = static_cast<uint8_t>(version);
}

} // namespace

TEST("a client joins, is given its character and sees the server's world") {
    Match match;
    // Moved after both loaded the world: the client learns of it from the server alone.
    match.server.scene().get<Transform>(match.crates[1]).position.z = 4.0f;
    match.play(60, [&](int frame) {
        if (frame < 60) return;
        CHECK(match.clientNet().role() == NetRole::Client);
        CHECK(match.clientNet().isPlaying());
        CHECK(match.serverNet().playerCount() == 1);
        const EntityId mine = match.clientNet().localEntity();
        REQUIRE(mine);
        const EntityId theirs = sameSlot(match.server.scene(), mine);
        CHECK(theirs == match.walkers[0]);
        CHECK(match.serverNet().isDriven(theirs));
        CHECK(match.clientNet().isMine(mine));
        CHECK(!match.serverNet().isMine(theirs));
        CHECK_NEAR(positionOf(match.client.scene(), mine), positionOf(match.server.scene(), theirs), 0.01f);
        for (const EntityId crate : match.crates) {
            CHECK_NEAR(
                positionOf(match.client.scene(), sameSlot(match.client.scene(), crate)),
                positionOf(match.server.scene(), crate),
                0.002f
            );
        }
    });
}

TEST("a change on the server reaches the client, and a world at rest costs only a few bytes a packet") {
    Match match;
    std::vector<size_t> down;  // each snapshot's size, by the frame it passed
    Relay relay(match.port, [&](size_t, bool up, std::vector<uint8_t>& bytes) {
        if (!up) down.push_back(bytes.size());
        return true;
    });

    size_t atRest = 0, moving = 0, joining = 0;
    glm::vec3 seen(0.0f);
    match.play(200, [&](int frame) {
        Scene& server = match.server.scene();
        if (frame == 30) joining = *std::max_element(down.begin(), down.end());
        if (frame == 120) {
            atRest = *std::max_element(down.end() - 20, down.end());
            Transform& crate = server.get<Transform>(match.crates[1]);
            crate.position.y += 3.0f;
            Rigidbody::wake(bodyOf(server, match.crates[1]));
        }
        if (frame == 125) moving = *std::max_element(down.end() - 5, down.end());
        if (frame == 200) seen = positionOf(
            match.client.scene(),
            sameSlot(match.client.scene(), match.crates[1])
        );
    }, &relay);

    // It fell back where it was; the client saw it go up and come down.
    CHECK_NEAR(seen, positionOf(match.server.scene(), match.crates[1]), 0.002f);
    CHECK(atRest > 0);
    CHECK(atRest < 64);
    CHECK(moving > atRest + 8);
    // The world the client joined, every body whole, against one crate moving.
    CHECK(joining > moving);
}

TEST("a late joiner gets the whole world as it stands") {
    Match match;
    EntityId added;
    // The client joins at frame 60, after the server has moved a crate, destroyed one and made one.
    match.play(150, [&](int frame) {
        Scene& server = match.server.scene();
        if (frame == 10) {
            server.get<Transform>(match.crates[0]).position.z = -8.0f;
            added = addCrate(server, {5.0f, 0.5f, 5.0f});
            server.destroyEntity(match.crates[2]);
        }
        if (frame == 60) REQUIRE(match.client.join(loopback(match.port), TICK_RATE));
        if (frame < 150) return;
        Scene& client = match.client.scene();
        REQUIRE(match.clientNet().isPlaying());
        CHECK_NEAR(positionOf(client, sameSlot(client, match.crates[0])).z, -8.0f, 0.01f);
        CHECK(!client.isAlive(sameSlot(client, match.crates[2])));
        CHECK_NEAR(positionOf(client, sameSlot(client, added)), positionOf(server, added), 0.01f);
        int crates = 0;
        client.forEach<Rigidbody>([&](EntityId, Rigidbody& body) {
            crates += body.motion == RigidbodyMotion::Dynamic && !body.freezeRotation;
        });
        CHECK(crates == 3);
    }, nullptr, false);
}

TEST("a field arrives within its precision, and a zero arrives as zero") {
    Match match;
    match.play(150, [&](int frame) {
        if (frame == 30) {
            Transform& at = match.server.scene().get<Transform>(match.crates[0]);
            at.position.x = 1.234567f;
        }
    });

    Scene& client = match.client.scene();
    const EntityId copy = sameSlot(client, match.crates[0]);
    const Transform& server = match.server.scene().get<Transform>(match.crates[0]);
    CHECK_NEAR(client.get<Transform>(copy).position, server.position, 0.001f);
    CHECK_NEAR(client.get<Transform>(copy).rotation, server.rotation, 0.001f);
    CHECK(bodyOf(client, copy).linearVelocity == glm::vec3(0.0f));
    CHECK(bodyOf(client, copy).angularVelocity == glm::vec3(0.0f));
}

// Named, not anonymous: Clang warns of a reflected type of this file alone that its NAME goes unused.
namespace NetGame {

struct Health {
    int   points = 100;
    float armor  = 0.0f;
};

} // namespace NetGame

using NetGame::Health;

VKM_REFLECT_BEGIN(::NetGame::Health)
    VKM_F(points)
    VKM_F(armor)
VKM_REFLECT_END()

TEST("a project's component replicated by NetSchema::replicate travels") {
    NetSchema::get().replicate<Health>("Health");
    Match match;
    match.server.scene().add(match.crates[0], Health{42, 0.5f});

    match.play(60, [&](int frame) {
        if (frame == 40) match.server.scene().get<Health>(match.crates[0]).points = 7;
    });

    const Health* copy = match.client.scene().tryGet<Health>(sameSlot(match.client.scene(), match.crates[0]));
    REQUIRE(copy);
    CHECK(copy->points == 7);
    CHECK_NEAR(copy->armor, 0.5f, 0.01f);
}

TEST("a NetLocal entity and a static body stay off the wire") {
    Match match;
    Scene& server = match.server.scene();
    // Made alike on both ends before the join, as a scene file would.
    std::vector<EntityId> local, still;
    for (Game* game : {&match.server, &match.client}) {
        Scene& scene = game->scene();
        const EntityId spinner = addCrate(scene, {8.0f, 3.0f, 0.0f});
        bodyOf(scene, spinner).gravityScale = 0.0f;
        scene.add(spinner, NetLocal{});
        local.push_back(spinner);
        still.push_back(addBody(scene, {-8.0f, 1.0f, 0.0f}, box(glm::vec3(0.5f)), RigidbodyMotion::Static));
    }

    match.play(60, [&](int frame) {
        if (frame == 20) {
            server.get<Transform>(local[0]).position.y = 9.0f;
            server.get<Transform>(still[0]).position.y = 9.0f;
        }
        if (frame < 60) return;
        Scene& client = match.client.scene();
        REQUIRE(match.clientNet().isPlaying());
        CHECK(positionOf(client, local[1]).y == 3.0f);
        CHECK(positionOf(client, still[1]).y == 1.0f);
        CHECK(match.clientNet().simulates(client, local[1]));
        CHECK(!match.clientNet().simulates(client, sameSlot(client, match.crates[0])));
    });
}

TEST("a client moves its own character at once, and is put where the server says") {
    Match match;
    float movedBeforeAnswer = 0.0f;
    glm::vec3 corrected(0.0f);
    match.play(120, [&](int frame) {
        Scene& client = match.client.scene();
        const EntityId mine = match.clientNet().localEntity();
        if (frame == 40) match.hold(Key::D);
        // Two frames after the press: the server has not yet seen it, the client already walks.
        if (frame == 42) movedBeforeAnswer = positionOf(client, mine).x - positionOf(
            match.server.scene(),
            characterIn(match.server.scene())
        ).x;
        if (frame == 60) match.hold(Key::D, false);
        if (frame == 80) {
            Scene& server = match.server.scene();
            server.get<Transform>(characterIn(server)).position = {3.0f, 0.9f, 6.0f};
        }
        if (frame == 120) corrected = positionOf(client, mine);
    });

    CHECK(movedBeforeAnswer > 0.005f);
    CHECK_NEAR(corrected, positionOf(match.server.scene(), characterIn(match.server.scene())), 0.02f);
    CHECK_NEAR(corrected.z, 6.0f, 0.05f);
}

TEST("a client predicts the bodies its character pushes") {
    Match match;
    size_t leased = 0;
    float ahead   = 0.0f;
    // The character walks into the first crate, 4 m ahead of it along -Z... from x = -2.
    match.play(150, [&](int frame) {
        Scene& client = match.client.scene();
        if (frame == 40) {
            match.server.scene().get<Transform>(match.crates[0]).position = {-2.0f, 0.5f, -1.5f};
            bodyOf(match.server.scene(), match.crates[0]).mass = 0.5f;
        }
        if (frame == 80) match.hold(Key::W);
        if (frame >= 80) leased = std::max(leased, match.clientNet().leaseCount());
        if (frame == 110) {
            const EntityId crate = sameSlot(client, match.crates[0]);
            ahead = positionOf(match.server.scene(), match.crates[0]).z - positionOf(client, crate).z;
        }
    });

    CHECK(leased >= 1);
    // The client's crate is pushed on before the server's word of it arrives.
    CHECK(ahead > 0.0f);
}

TEST("what the client does not own is drawn between two server poses, a little behind") {
    Match match;
    std::vector<float> drawn;
    match.play(120, [&](int frame) {
        Scene& server = match.server.scene();
        // A kinematic body the server moves at 3 m/s.
        if (frame == 30) bodyOf(server, match.crates[2]).motion = RigidbodyMotion::Kinematic;
        if (frame > 30) {
            server.get<Transform>(match.crates[2]).position.x += 3.0f / TICK_RATE;
            bodyOf(server, match.crates[2]).linearVelocity = {3.0f, 0.0f, 0.0f};
        }
        if (frame > 70) drawn.push_back(
            positionOf(match.client.scene(), sameSlot(match.client.scene(), match.crates[2])).x
        );
        if (frame == 120) CHECK(match.clientNet().renderDelay() > 0.0f);
    });

    REQUIRE(drawn.size() > 10);
    // Every frame a step of the same size: drawn between poses, not jumping to each as it lands.
    for (size_t i = 1; i < drawn.size(); ++i) CHECK_NEAR(drawn[i] - drawn[i - 1], 3.0f / TICK_RATE, 0.01f);
    CHECK(drawn.back() < positionOf(match.server.scene(), match.crates[2]).x);
}

TEST("a client's tick clock runs fast while the server's queue of its commands runs short") {
    Match match;
    float most = 0.0f, servers = 0.0f;
    // Its commands lost for a while: the server's queue empties.
    Relay relay(
        match.port,
        [](size_t index, bool up, std::vector<uint8_t>&) { return !(up && index >= 100 && index < 140); }
    );
    match.play(200, [&](int frame) {
        if (frame > 100) most = std::max(most, match.clientNet().pacing());
        if (frame > 100) servers = std::max(servers, match.serverNet().pacing());
    }, &relay);

    CHECK(most > 1.0f);
    CHECK(servers == 1.0f);
}

TEST("a jump pressed on one tick reaches the server though most of the packets carrying it are lost") {
    Match match;
    // Two in three of the client's packets dropped.
    Relay relay(
        match.port,
        [](size_t index, bool up, std::vector<uint8_t>&) { return !up || index % 3 == 0; }
    );
    float highest = 0.0f;
    match.play(150, [&](int frame) {
        if (frame == 80) match.hold(Key::Space);
        if (frame == 81) match.hold(Key::Space, false);
        if (frame > 80) highest = std::max(
            highest,
            positionOf(match.server.scene(), characterIn(match.server.scene())).y
        );
    }, &relay);

    CHECK(highest > 1.5f);
}

TEST("a command delivered twice presses once") {
    Match match;
    int jumps = 0;
    bool wasJumping = false;
    // Every packet from the client arrives twice.
    Relay relay(match.port);
    relay.duplicateUp(true);
    float serverWalked = 0.0f, clientWalked = 0.0f;
    match.play(220, [&](int frame) {
        if (frame == 40) match.hold(Key::D);
        if (frame == 60) match.hold(Key::D, false);
        if (frame == 79) {
            serverWalked = positionOf(match.server.scene(), match.walkers[0]).x + 2.0f;
            clientWalked = positionOf(match.client.scene(), match.clientNet().localEntity()).x + 2.0f;
        }
        if (frame == 80 || frame == 170) match.hold(Key::Space);
        if (frame == 81 || frame == 171) match.hold(Key::Space, false);
        const bool jumping = match.server.scene().get<CharacterController>(
            characterIn(match.server.scene())
        ).jumping;
        if (jumping && !wasJumping) ++jumps;
        wasJumping = jumping;
    }, &relay);

    CHECK(clientWalked > 1.0f);
    CHECK_NEAR(serverWalked, clientWalked, 0.05f);
    CHECK(Walker::serverPresses == 2);
    CHECK(jumps == 2);
}

TEST("messages arrive once each and in order over a lossy link, and one over 255 bytes is refused") {
    Match match;
    std::vector<uint8_t> heard;
    match.server.events().subscribe<PlayerMessageEvent>([&](const PlayerMessageEvent& said) {
        CHECK(said.bytes.size() == 2);
        CHECK(said.entity == characterIn(match.server.scene()));
        heard.push_back(said.bytes[1]);
    });
    Relay relay(
        match.port,
        [](size_t index, bool up, std::vector<uint8_t>&) { return !up || index % 3 != 1; }
    );
    bool tooLong = true, offline = true;
    uint8_t sent = 0;
    match.play(240, [&](int frame) {
        if (frame >= 60 && frame < 100) {
            const uint8_t message[] = {7, sent};
            if (match.clientNet().sendMessage(message, sizeof message)) ++sent;
        }
        if (frame == 100) {
            std::vector<uint8_t> big(256, 1);
            const uint8_t one = 1;
            tooLong = match.clientNet().sendMessage(big.data(), big.size());
            offline = match.serverNet().sendMessage(&one, 1);
        }
    }, &relay);

    CHECK(!tooLong);
    CHECK(!offline);
    CHECK(sent > 20);
    REQUIRE(heard.size() == sent);
    for (size_t i = 0; i < heard.size(); ++i) CHECK(heard[i] == i);
}

TEST("worldTime reads alike on the server and its client, however long each has run") {
    Match match;
    // The client's own clock half a second ahead, as one started before the server would be.
    match.client.clock().requestStep(30);
    double serverTime = 0.0, clientTime = 0.0;
    match.play(120, [&](int frame) {
        if (frame != 120) return;
        serverTime = worldTime(match.serverNet(), match.server.clock());
        clientTime = worldTime(match.clientNet(), match.client.clock());
    });

    // The client draws the server's world a few ticks behind it.
    CHECK(serverTime > 1.5);
    CHECK(clientTime <= serverTime);
    CHECK(serverTime - clientTime < 10.0 / TICK_RATE);
}

TEST("a lost connection holds the client's world still and says what happened") {
    Match match;
    Relay relay(match.port);
    glm::vec3 heldAt(0.0f);
    EntityId mine;
    match.play(1200, [&](int frame) {
        if (frame == 30) mine = match.clientNet().localEntity();
        if (frame == 60) relay.cut();
        if (frame == 1100) heldAt = positionOf(match.client.scene(), mine);
        if (frame < 1200) return;
        CHECK(match.clientNet().isDisconnected());
        CHECK(!match.clientNet().lastError().empty());
        CHECK(!match.clientNet().simulates(match.client.scene(), mine));
        CHECK(positionOf(match.client.scene(), mine) == heldAt);
        CHECK(match.serverNet().playerCount() == 0);
    }, &relay);
    CHECK(mine);
}

TEST("forged, cut short and stray datagrams stop neither the server nor its client") {
    Match match;
    Relay relay(match.port, [](size_t index, bool, std::vector<uint8_t>& bytes) {
        // One datagram either way forged once both ends play: numbered far ahead, with the wrong token.
        // Accepted, it would move the connection's window past every real datagram after it.
        constexpr size_t SEQUENCE_HIGH = 1, TOKEN = 12;
        if (index == 30 && bytes.size() > TOKEN) {
            bytes[SEQUENCE_HIGH] = static_cast<uint8_t>(bytes[SEQUENCE_HIGH] + 0x40);
            bytes[TOKEN] ^= 0x01;
        }
        if (index % 11 == 3) bytes.resize(bytes.size() / 2);
        return true;
    });
    UdpSocket stranger;
    REQUIRE(stranger.open(0));
    float walked = 0.0f;
    match.play(300, [&](int frame) {
        // Junk at the server from somewhere else: empty, short, long, a stretch of every byte.
        std::vector<uint8_t> junk(static_cast<size_t>(frame * 37 % 1300), static_cast<uint8_t>(frame));
        stranger.send(loopback(match.port), junk.data(), junk.size());
        if (frame == 150) match.hold(Key::D);
        if (frame < 300) return;
        walked = positionOf(match.server.scene(), match.walkers[0]).x + 2.0f;
        CHECK(match.clientNet().isPlaying());
        CHECK(match.serverNet().playerCount() == 1);
    }, &relay);

    CHECK(walked > 2.0f);
}

TEST("a Hello forged in a joining client's name gets it no refusal: only an echoed token is answered") {
    Match match;
    // What the server says to the client's address: a Refuse there, for a Hello no one proved, is the fault.
    std::vector<NetMessage> answers;
    Relay relay(match.port, [&](size_t index, bool up, std::vector<uint8_t>& bytes) {
        // The client's first Hello, as another build would send it from the client's address: another
        // version of the wire, and a body this one would refuse.
        constexpr size_t VERSION = 10, KIND = 11, TOKEN = 12;
        if (up && index == 0 && bytes.size() > TOKEN) {
            bytes[VERSION] = static_cast<uint8_t>(NET_PROTOCOL_VERSION + 1);
            std::fill(bytes.begin() + TOKEN + 1, bytes.end(), uint8_t{0});
        }
        if (!up && bytes.size() > KIND) answers.push_back(static_cast<NetMessage>(bytes[KIND]));
        return true;
    });
    match.play(120, [&](int frame) {
        if (frame < 120) return;
        CHECK(match.clientNet().isPlaying());
        CHECK(match.serverNet().playerCount() == 1);
    }, &relay);
    CHECK(std::find(answers.begin(), answers.end(), NetMessage::Refuse) == answers.end());
}

TEST("a Refuse forged before a joining client's Challenge turns it away at no version of the wire") {
    for (const int version : {int{NET_PROTOCOL_VERSION}, NET_PROTOCOL_VERSION + 1}) {
        Match match;
        // The client's first answer, its Challenge, as a forger in the server's name would send it: a
        // Refuse, guessing the token.
        Relay relay(match.port, [&](size_t index, bool up, std::vector<uint8_t>& bytes) {
            if (!up && index == 0) makeRefusal(bytes, 0, version);
            return true;
        });
        match.play(120, [&](int frame) {
            if (frame < 120) return;
            CHECK(match.clientNet().isPlaying());
            CHECK(match.serverNet().playerCount() == 1);
        }, &relay);
    }
}

TEST("a Challenge and then a Refuse forged in the server's name turn a joining client away at no version of the wire") {
    for (const int version : {int{NET_PROTOCOL_VERSION}, NET_PROTOCOL_VERSION + 1}) {
        Match match;
        // A forger off the path, who knows both addresses but never saw the client's Hello: a Challenge
        // giving a token of its choosing, guessing what it echoes, then a Refuse carrying that token.
        constexpr uint64_t FORGED = 0x0123456789ABCDEFull;
        Relay relay(match.port, [&](size_t index, bool up, std::vector<uint8_t>& bytes) {
            if (!up && index == 0) makeChallenge(bytes, FORGED, 0, version);
            if (!up && index == 1) makeRefusal(bytes, FORGED, version);
            return true;
        });
        match.play(120, [&](int frame) {
            if (frame < 120) return;
            CHECK(match.clientNet().isPlaying());
            CHECK(match.serverNet().playerCount() == 1);
        }, &relay);
    }
}

TEST("a Challenge echoing a token other than the client's is ignored, at any version of the wire") {
    for (const int version : {int{NET_PROTOCOL_VERSION}, NET_PROTOCOL_VERSION + 1}) {
        Match match;
        // Every Challenge the server sends, its token kept and its echo changed: taken, the token would
        // seat the client.
        Relay relay(match.port, [&](size_t, bool up, std::vector<uint8_t>& bytes) {
            constexpr size_t HEADER = Connection::HEADER_BYTES, KIND = 11;
            if (up || bytes.size() <= KIND || bytes[KIND] != static_cast<uint8_t>(NetMessage::Challenge)) {
                return true;
            }
            BitReader  reader(bytes.data() + HEADER, bytes.size() - HEADER);
            NetMessage message = NetMessage::Challenge;
            uint64_t   token   = 0;
            readMessageHeader(reader, message, token);
            makeChallenge(bytes, token, reader.u64() + 1, version);
            return true;
        });
        match.play(120, [&](int frame) {
            if (frame < 120) return;
            CHECK(!match.clientNet().isPlaying());
            CHECK(match.serverNet().playerCount() == 0);
        }, &relay);
    }
}

TEST("a server that refuses another version of the wire at once, echoing the Hello's token, is heard") {
    Match match;
    // An earlier build's server: its answer to the first Hello is a refusal carrying that Hello's token.
    uint64_t hello = 0;
    Relay relay(match.port, [&](size_t index, bool up, std::vector<uint8_t>& bytes) {
        constexpr size_t HEADER = Connection::HEADER_BYTES;
        if (up && index == 0) {
            BitReader  reader(bytes.data() + HEADER, bytes.size() - HEADER);
            NetMessage message = NetMessage::Hello;
            readMessageHeader(reader, message, hello);
        }
        if (!up) makeRefusal(bytes, hello, NET_PROTOCOL_VERSION + 1);
        return true;
    });
    match.play(30, [&](int frame) {
        if (frame < 30) return;
        CHECK(match.clientNet().isDisconnected());
        CHECK(match.clientNet().lastError() == toString(NetRefusal::Mismatch));
    }, &relay);
}

TEST("a join from another build is refused once it echoes its token, and told why") {
    Match match;
    // Every datagram either way at another version of the wire, as two builds would see each other's.
    std::vector<NetMessage> answers;
    Relay relay(match.port, [&](size_t, bool up, std::vector<uint8_t>& bytes) {
        constexpr size_t VERSION = 10, KIND = 11;
        if (bytes.size() <= KIND) return true;
        bytes[VERSION] = static_cast<uint8_t>(NET_PROTOCOL_VERSION + 1);
        if (!up) answers.push_back(static_cast<NetMessage>(bytes[KIND]));
        return true;
    });
    match.play(60, [&](int frame) {
        if (frame < 60) return;
        CHECK(match.clientNet().isDisconnected());
        CHECK(match.clientNet().lastError() == toString(NetRefusal::Mismatch));
        CHECK(match.serverNet().playerCount() == 0);
    }, &relay);

    // Challenged first, as anyone is, and refused only once its Hello echoed the token.
    REQUIRE(!answers.empty());
    CHECK(answers.front() == NetMessage::Challenge);
    CHECK(answers.back() == NetMessage::Refuse);
}

TEST("a client of release 1.1.0 is refused by this build's server as another build, and told why") {
    constexpr uint8_t WIRE_1_1_0 = 14;  // NET_PROTOCOL_VERSION in release 1.1.0

    Game server;
    REQUIRE(server.host(0, 4, TICK_RATE));
    const uint16_t port = server.engine.getNet().localAddress().port;
    UdpSocket player;
    REQUIRE(player.open(0));

    // 1.1.0's Hello: its header at its version, carrying zero until a Challenge gives it a token, then
    // its body, of which this build reads nothing.
    const auto hello = [&](uint64_t token) {
        std::vector<uint8_t> payload;
        BitWriter writer(payload, 64);
        writer.u16(NET_PROTOCOL_TAG);
        writer.u8(WIRE_1_1_0);
        writer.u8(static_cast<uint8_t>(NetMessage::Hello));
        writer.u64(token);
        writer.u32(0);
        writer.u8(0);
        writer.u64(0);
        writer.u64(0);
        writer.u16(static_cast<uint16_t>(TICK_RATE));
        writer.finish();
        std::vector<uint8_t> datagram(Connection::HEADER_BYTES, 0);
        datagram.insert(datagram.end(), payload.begin(), payload.end());
        player.send(loopback(port), datagram.data(), datagram.size());
    };

    // 1.1.0's reading of an answer: a Challenge only at its own version, taking the token it carries; a
    // Refuse at any version, once it carries that token, and then its reason.
    uint64_t token = 0;
    bool refused = false;
    NetRefusal reason = NetRefusal::Count;
    const auto hear = [&] {
        const size_t header = Connection::HEADER_BYTES;
        std::vector<uint8_t> datagram;
        Address from;
        while (player.receive(datagram, from)) {
            if (datagram.size() < header) continue;
            BitReader reader(datagram.data() + header, datagram.size() - header);
            const uint16_t tag = reader.u16();
            const uint8_t version = reader.u8();
            const auto kind = static_cast<NetMessage>(reader.u8());
            const uint64_t carried = reader.u64();
            if (reader.failed() || tag != NET_PROTOCOL_TAG) continue;
            if (kind == NetMessage::Challenge && version == WIRE_1_1_0) {
                token = carried;
            } else if (kind == NetMessage::Refuse && carried == token) {
                reason  = static_cast<NetRefusal>(reader.u8());
                refused = true;
            }
        }
    };

    server.run(8, [&](int frame) {
        if (frame > 1) hear();
        if (!refused) hello(token);
    });

    REQUIRE(refused);
    CHECK(reason == NetRefusal::Mismatch);
}

TEST("a join at another tick rate is refused, saying why") {
    Match match(1, 30);
    match.play(60, [&](int frame) {
        if (frame < 60) return;
        CHECK(match.clientNet().isDisconnected());
        CHECK(match.clientNet().lastError().find("tick") != std::string::npos);
        CHECK(match.serverNet().playerCount() == 0);
    });
}

TEST("a join from another world is refused, saying so") {
    Match match(2);
    match.play(60, [&](int frame) {
        if (frame < 60) return;
        CHECK(match.clientNet().isDisconnected());
        CHECK(!match.clientNet().lastError().empty());
        CHECK(match.serverNet().playerCount() == 0);
    });
}

TEST("a join with another set of actions is refused, and the server logs the first refusal once") {
    Match match;
    match.client.engine.getInput().setActions({{"Fire", {InputBinding::key(Key::F)}}});
    Relay relay(match.port);
    relay.duplicateUp(true);
    match.play(120, [&](int frame) {
        if (frame < 120) return;
        CHECK(match.clientNet().isDisconnected());
        CHECK(match.serverNet().playerCount() == 0);
        const std::string said = match.serverNet().describe();
        const size_t refused   = said.find("refused ");
        REQUIRE(refused != std::string::npos);
        CHECK(std::atoi(said.c_str() + refused + 8) >= 2);
    }, &relay);
    CHECK(countInLog("Refusing") == 1);
}

TEST("a join replayed from another address seats no one") {
    Match match;
    std::vector<std::vector<uint8_t>> heard;
    Relay relay(match.port, [&](size_t, bool up, std::vector<uint8_t>& bytes) {
        if (up && heard.size() < 40) heard.push_back(bytes);
        return true;
    });
    UdpSocket spoof;
    REQUIRE(spoof.open(0));
    match.play(120, [&](int frame) {
        if (frame >= 60 && frame < 100) {
            for (const std::vector<uint8_t>& bytes : heard) spoof.send(
                loopback(match.port),
                bytes.data(),
                bytes.size()
            );
        }
        if (frame == 120) CHECK(match.serverNet().playerCount() == 1);
    }, &relay);
}

TEST("a token from one hosting is no proof to the next: each host keys its tokens afresh") {
    Game server;
    REQUIRE(server.host(0, 4, TICK_RATE));
    const uint16_t port = server.engine.getNet().localAddress().port;
    UdpSocket player;
    REQUIRE(player.open(0));

    // A Hello carrying @p token, with a body of zeros, which no game's vetting passes.
    const auto hello = [&](uint64_t token) {
        std::vector<uint8_t> payload;
        BitWriter writer(payload, 64);
        writeMessageHeader(writer, NetMessage::Hello, token);
        for (int i = 0; i < 32; ++i) writer.u8(0);
        writer.finish();
        std::vector<uint8_t> datagram(Connection::HEADER_BYTES, 0);
        datagram.insert(datagram.end(), payload.begin(), payload.end());
        player.send(loopback(port), datagram.data(), datagram.size());
    };
    // What the server answered, and the token the answer carried.
    NetMessage answered = NetMessage::Hello;
    uint64_t token = 0;
    const auto hear = [&] {
        const size_t header = Connection::HEADER_BYTES;
        std::vector<uint8_t> datagram;
        Address from;

        answered = NetMessage::Hello;
        if (!player.receive(datagram, from) || datagram.size() < header) return;
        BitReader reader(datagram.data() + header, datagram.size() - header);
        readMessageHeader(reader, answered, token);
    };

    NetMessage challenged = NetMessage::Hello, vetted = NetMessage::Hello, rehosted = NetMessage::Hello;
    server.run(4, [&](int frame) {
        if (frame > 1) hear();
        if (frame == 2) challenged = answered;
        // The token echoed proves the address: vetted, and refused for its body.
        if (frame == 3) vetted = answered;
        if (frame == 3) REQUIRE(server.host(port, 4, TICK_RATE));
        if (frame == 4) rehosted = answered;
        hello(frame == 1 ? 0 : token);
    });

    CHECK(challenged == NetMessage::Challenge);
    CHECK(vetted == NetMessage::Refuse);
    CHECK(rehosted == NetMessage::Challenge);
}

TEST_WITH_INFO_LOG("a welcome said again seats the client once") {
    Match match;
    // A slow link: the client says Hello a few times before the first Welcome lands, and the server
    // answers each with a Welcome.
    Relay relay(match.port, {}, 4);
    match.play(120, [&](int frame) {
        if (frame < 120) return;
        CHECK(match.clientNet().isPlaying());
        CHECK(match.serverNet().playerCount() == 1);
    }, &relay);
    CHECK(countInLog("Joined as player") == 1);
}

TEST("a client that leaves and joins again is seated afresh") {
    Match match;
    match.play(200, [&](int frame) {
        if (frame == 60) match.clientNet().close();
        if (frame == 100) REQUIRE(match.client.join(loopback(match.port), TICK_RATE));
        if (frame < 200) return;
        CHECK(match.serverNet().playerCount() == 1);
        CHECK(match.clientNet().isPlaying());
        CHECK(match.clientNet().localEntity());
    });
}

TEST("every reason an entity stays off the wire has words, and being on it has none") {
    const NetSilence reasons[] = {
        NetSilence::None,
        NetSilence::InsidePrefabInstance,
        NetSilence::PosedByAnimation,
        NetSilence::StaticBody,
        NetSilence::FixedMesh,
        NetSilence::Local,
    };
    for (const NetSilence reason : reasons) {
        const NetSilenceText text = explain(reason);
        if (reason == NetSilence::None) {
            CHECK(text.label == nullptr);
            CHECK(text.explanation == nullptr);
            continue;
        }
        REQUIRE(text.label);
        REQUIRE(text.explanation);
        CHECK(std::strlen(text.label) > 0);
        CHECK(std::strlen(text.explanation) > 0);
    }
}

TEST("Rewind puts the other players back where the shooter saw them, for one query") {
    Match match;
    float present = 0.0f, rewound = 0.0f, after = 0.0f, onClient = 0.0f, onClientRewound = 0.0f;
    match.playTwo(150, [&](int frame) {
        if (frame == 30) match.hold(Key::W, true, &match.second);
        if (frame != 120) return;
        NetSession& net = match.serverNet();
        Scene& scene = match.server.scene();
        const EntityId shooter = sameSlot(scene, match.clientNet().localEntity());
        const EntityId target  = sameSlot(scene, match.second.engine.getNet().localEntity());
        REQUIRE(shooter == match.walkers[0]);
        REQUIRE(target == match.walkers[1]);
        present = positionOf(scene, target).z;
        const glm::vec3 shooterAt = positionOf(scene, shooter);
        {
            NetSession::Rewind back(net, scene, shooter);
            rewound = positionOf(scene, target).z;
            CHECK(positionOf(scene, shooter) == shooterAt);
        }
        after = positionOf(scene, target).z;
        // On a client the scope does nothing.
        Scene& client = match.client.scene();
        const EntityId theirs = sameSlot(client, target);
        onClient = positionOf(client, theirs).z;
        {
            NetSession::Rewind back(match.clientNet(), client, match.clientNet().localEntity());
            onClientRewound = positionOf(client, theirs).z;
        }
    }, 1);

    // It walked from z = 0 toward -Z: a few ticks back is some centimeters behind.
    CHECK(present < -1.0f);
    CHECK(rewound > present + 0.05f);
    CHECK(after == present);
    CHECK(onClientRewound == onClient);
}

TEST("a shared spawn builds the prefab on the server and every client, a later joiner too") {
    openProject();
    {
        Frame author;
        const EntityId root = addCrate(author.scene, {0.0f, 0.0f, 0.0f});
        const EntityId lid  = addBody(
            author.scene,
            {0.0f, 0.6f, 0.0f},
            box({0.5f, 0.1f, 0.5f}),
            RigidbodyMotion::Kinematic
        );
        HierarchyOperations::setParent(author.scene, lid, root);
        REQUIRE(Prefab::save(author.scene, root, "prefabs/chest.json", author.resources));
    }
    Match match;
    EntityId spawned;
    match.playTwo(160, [&](int frame) {
        if (frame == 40) {
            Transform at;
            at.position = {6.0f, 2.0f, 6.0f};
            spawned = match.serverNet().spawn(
                match.server.scene(),
                match.server.resources(),
                "prefabs/chest.json",
                at
            );
            REQUIRE(spawned);
            CHECK(!match.clientNet().spawn(
                match.client.scene(),
                match.client.resources(),
                "prefabs/chest.json",
                at
            ));
            CHECK(
                !match.serverNet().spawn(
                    match.server.scene(),
                    match.server.resources(),
                    "../outside.json",
                    at
                )
            );
        }
        if (frame < 160) return;
        for (Game* end : {&match.client, &match.second}) {
            REQUIRE(end->engine.getNet().isPlaying());
            Scene& scene = end->scene();
            const EntityId copy = sameSlot(scene, spawned);
            REQUIRE(scene.isAlive(copy));
            CHECK(scene.has<Collider>(copy));
            CHECK(childrenOf(scene, copy) == 1);
            CHECK_NEAR(positionOf(scene, copy), positionOf(match.server.scene(), spawned), 0.01f);
        }
    }, 100);
}

TEST("a client's own entity made before it joined is not written over by the server's in its slot") {
    Match match;
    EntityId mineAlone, serversNew;
    match.play(120, [&](int frame) {
        // Made offline, as a game's menu is before it joins from it: in the next free slot, which the
        // server then fills with its own.
        if (frame == 5) mineAlone = addCrate(match.client.scene(), {0.0f, 6.0f, 9.0f});
        if (frame == 10) REQUIRE(match.client.join(loopback(match.port), TICK_RATE));
        if (frame == 50) serversNew = addCrate(match.server.scene(), {4.0f, 6.0f, 9.0f});
        if (frame == 120) CHECK(match.clientNet().simulates(match.client.scene(), mineAlone));
    }, nullptr, false);

    Scene& client = match.client.scene();
    REQUIRE(client.isAlive(mineAlone));
    // Where it landed, a few millimeters at most from where it fell: the server's is 4 m away.
    CHECK_NEAR(positionOf(client, mineAlone).x, 0.0f, 0.05f);
    // The server's new crate is on the client too, wherever it found room.
    int at4 = 0;
    client.forEach<Rigidbody>(
        [&](EntityId id, Rigidbody&) { at4 += std::fabs(positionOf(client, id).x - 4.0f) < 0.01f; }
    );
    CHECK(at4 == 1);
    CHECK(serversNew.slot() == mineAlone.slot());
}

TEST("a client's own entities made while it plays leave the server's slots free, so a spawn lands in them") {
    openProject();
    {
        Frame author;
        const EntityId root = addCrate(author.scene, {0.0f, 0.0f, 0.0f});
        const EntityId lid  = addBody(
            author.scene,
            {0.0f, 0.6f, 0.0f},
            box({0.5f, 0.1f, 0.5f}),
            RigidbodyMotion::Kinematic
        );
        HierarchyOperations::setParent(author.scene, lid, root);
        REQUIRE(Prefab::save(author.scene, root, "prefabs/chest.json", author.resources));
    }
    Match match;
    EntityId spawned;
    match.play(120, [&](int frame) {
        // A HUD and an effect of the client's own, then the server's spawn.
        if (frame == 30) {
            for (int i = 0; i < 4; ++i) match.client.scene().createEntity();
        }
        if (frame == 40) {
            Game& server = match.server;
            Transform at;
            at.position = {6.0f, 2.0f, 6.0f};

            spawned = match.serverNet().spawn(server.scene(), server.resources(), "prefabs/chest.json", at);
        }
    });
    REQUIRE(spawned);
    const Scene& client = match.client.scene();
    const EntityId copy = sameSlot(match.client.scene(), spawned);
    REQUIRE(client.isAlive(copy));
    CHECK(client.has<PrefabInstance>(copy));
    CHECK(Prefab::builtSlotsOf(client, copy) == Prefab::builtSlotsOf(match.server.scene(), spawned));
    CHECK(countInLog("was not built into every slot") == 0);
}

TEST("once its session ends, a client's own entities take any free slot again") {
    Match match;
    uint32_t keptBelow = 0;
    match.play(60, [&](int frame) {
        if (frame == 30) keptBelow = match.client.scene().slotFloor();
        if (frame == 40) match.clientNet().close();
    });

    Scene& client = match.client.scene();
    const uint32_t freed = match.crates[0].slot();
    REQUIRE(freed < keptBelow);
    client.destroyEntity(client.entityAt(freed));
    CHECK(client.createEntity().slot() == freed);
}
