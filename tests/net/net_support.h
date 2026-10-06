#pragma once

// Shared by the networking suites and the few elsewhere that stand up a session:
// sockets, snapshot round trips and the worlds they carry.

#include "support.h"

#include <cstdint>
#include <deque>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/component/physics/rigidbody.h"
#include "net/net_session.h"
#include "net/prediction/command.h"
#include "net/replication/silence.h"
#include "net/replication/snapshot.h"
#include "net/wire/bit_stream.h"
#include "net/wire/codecs.h"
#include "net/wire/schema.h"
#include "platform/net/udp_socket.h"
#include "platform/net/winsock_init.h"
#include "platform/windows_api.h"

// UdpSocket::send refuses zero-length datagrams; a peer elsewhere need not, so
// testing one arriving means sending it the way the network can.
#if !defined(_WIN32)
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

// A socket for the datagrams UdpSocket refuses to emit. A signed descriptor on POSIX
// and an unsigned handle on Windows, where `>= 0` is always true.
#if defined(_WIN32)
    using RawSocket = SOCKET;
    inline bool rawSocketOpen(RawSocket s)  { return s != INVALID_SOCKET; }
    inline void rawSocketClose(RawSocket s) { ::closesocket(s); }
#else
    using RawSocket = int;
    inline bool rawSocketOpen(RawSocket s)  { return s >= 0; }
    inline void rawSocketClose(RawSocket s) { ::close(s); }
#endif

// Opened through here: on Windows the library must be started before its first entry
// point, whichever socket a suite happens to open first.
inline RawSocket rawSocketUdp() {
    ensureWinsock();
    return ::socket(AF_INET, SOCK_DGRAM, 0);
}

/**
 * @brief Apply @p body, one whole snapshot, to @p scene through readSnapshot.
 *
 * @param scene  World the snapshot is applied to.
 * @param schema Wire order the body was written in.
 * @param body   The snapshot body, header excluded.
 * @return False when readSnapshot found the body malformed.
 */
inline bool readSnapshotFrom(Scene& scene, const NetSchema& schema, const std::vector<uint8_t>& body) {
    BitReader reader(body.data(), body.size());
    return readSnapshot(scene, schema, reader);
}

/**
 * @brief writeSnapshot as one peer's share of a send round, into @p body alone.
 *
 * Classifies the world afresh, as a server does once a round. The writer has room
 * past the budget, so a test sees an overrun rather than a refusal.
 *
 * @param scene    World being sent.
 * @param schema   Wire order to write in.
 * @param sequence Sequence number the snapshot carries.
 * @param budget   The peer's budget, which the writer may overrun by 64 bytes.
 * @param baseline The peer's baseline, which gains a pending record of this snapshot.
 * @param body     Filled with the written snapshot.
 * @return What writeSnapshot reported.
 */
inline NetSnapshotStats writeSnapshotTo(
    const Scene& scene,
    const NetSchema& schema,
    uint16_t sequence,
    const NetBudget& budget,
    NetBaseline& baseline,
    std::vector<uint8_t>& body
) {
    NetSilenceMap silence;
    silence.build(scene);
    BitWriter writer(body, budget.bytes + 64);
    const NetSnapshotStats stats =
        writeSnapshot(scene, schema, silence, sequence, budget, baseline, writer);
    writer.finish();
    return stats;
}

/**
 * @brief A world of @p count crates in a line, each with a body.
 *
 * Slot zero is the reserved null, so the first crate is not entity zero.
 *
 * @param scene World the crates are created in.
 * @param count How many to create, 1.5 m apart along +X.
 * @return Their ids, in creation order.
 */
inline std::vector<EntityId> buildCrates(Scene& scene, int count) {
    std::vector<EntityId> crates;
    for (int i = 0; i < count; ++i) {
        const EntityId crate = scene.createEntity();
        Transform at;
        at.position = {static_cast<float>(i) * 1.5f, 4.0f, 0.0f};
        scene.add(crate, at);
        scene.add(crate, Rigidbody{});
        crates.push_back(crate);
    }
    return crates;
}

/**
 * @brief The engine's own wire types, registered for one test and gone after it.
 *
 * The schema is process-global; clearing it by hand would miss a failing test's
 * early returns.
 */
class ScopedEngineSchema {
    public:
        ScopedEngineSchema() {
            NetSchema::get().clear();
            registerEngineNetTypes();
        }
        ~ScopedEngineSchema() { NetSchema::get().clear(); }

        ScopedEngineSchema(const ScopedEngineSchema& other) = delete;
        ScopedEngineSchema& operator=(const ScopedEngineSchema& other) = delete;

        ScopedEngineSchema(ScopedEngineSchema && other) = delete;
        ScopedEngineSchema& operator=(ScopedEngineSchema && other) = delete;
};

/**
 * @brief A body a rule moves, rather than a solver.
 *
 * Kinematic, so the motion comes from the test, not from physics deciding something.
 *
 * @param scene World the walker is created in, at the origin.
 * @return The walker's id.
 */
inline EntityId addWalker(Scene& scene) {
    const EntityId walker = scene.createEntity();
    Transform at;
    scene.add(walker, at);
    Rigidbody body;
    body.motion = RigidbodyMotion::Kinematic;
    scene.add(walker, body);
    return walker;
}

inline InputCommand walkCommand(uint32_t sequence, uint32_t tick, float forward, bool jump) {
    InputCommand command;
    command.sequence = sequence;
    command.tick     = tick;
    command.axis[0]  = forward;
    command.axis[1]  = 0.0f;
    if (jump) command.pressed = 1u << 2;
    command.view = glm::angleAxis(glm::radians(static_cast<float>(tick)), glm::vec3(0.0f, 1.0f, 0.0f));
    return command;
}

constexpr uint32_t TEST_ACTIONS = 6;

constexpr uint32_t TEST_TICK_RATE = 60;

/**
 * @brief Pump both ends until @p done, or give up.
 *
 * Loopback delivers within a frame; the cap makes a broken build fail rather than hang.
 *
 * @tparam Step   Callable run once a round.
 * @tparam Done   Predicate asked after each round.
 * @param step    One round of pumping both ends.
 * @param done    Whether what the test waits for has happened.
 * @param rounds  Rounds to try before giving up.
 * @return True if @p done answered yes within @p rounds.
 */
template <typename Step, typename Done>
inline bool pumpUntil(Step step, Done done, int rounds = 40) {
    for (int i = 0; i < rounds; ++i) {
        step();
        if (done()) return true;
    }
    return false;
}

/**
 * @brief Two sockets between a client and a server, holding every datagram a fixed
 *        number of rounds each way.
 *
 * The client joins the front socket; the server sees the back one as the client.
 * Every datagram waits the same number of rounds, so what is due is always at the front.
 */
class DelayedLink {
    public:
        DelayedLink(uint16_t serverPort, int rounds)
            : m_server{0x7F000001u, serverPort}
            , m_rounds(rounds)
        {
            m_open = m_front.open(0) && m_back.open(0);
        }
        ~DelayedLink() = default;

        DelayedLink(const DelayedLink& other) = delete;
        DelayedLink& operator=(const DelayedLink& other) = delete;

        DelayedLink(DelayedLink && other) = delete;
        DelayedLink& operator=(DelayedLink && other) = delete;

    public:
        /// Send @p bytes to the server from the address it knows the client by.
        void forge(const std::vector<uint8_t>& bytes) {
            m_back.send(m_server, bytes.data(), bytes.size());
        }

        /// Send @p bytes to the client from the address it knows the server by.
        void forgeToClient(const std::vector<uint8_t>& bytes) {
            if (m_client) m_front.send(m_client, bytes.data(), bytes.size());
        }

        /// One round: take what reached either socket, pass on what is due.
        void pump() {
            ++m_round;
            take(m_front, true);
            take(m_back, false);
            while (!m_held.empty() && m_held.front().due <= m_round) {
                const Held& held = m_held.front();
                if (held.towardServer) {
                    m_back.send(m_server, held.bytes.data(), held.bytes.size());
                } else if (m_client) {
                    m_front.send(m_client, held.bytes.data(), held.bytes.size());
                    m_lastToClient = held.bytes;
                }
                m_held.pop_front();
            }
        }

    public:
        bool       isOpen() const { return m_open; }
        NetAddress front() const  { return m_front.localAddress(); }

        /// The newest datagram passed on to the client, header and all.
        const std::vector<uint8_t>& lastToClient() const { return m_lastToClient; }

    private:
        struct Held {
            int                  due = 0;
            bool                 towardServer = false;
            std::vector<uint8_t> bytes;
        };

    private:
        void take(UdpSocket& socket, bool towardServer) {
            std::vector<uint8_t> bytes;
            NetAddress from;
            while (socket.receive(bytes, from)) {
                if (bytes.empty()) continue;
                if (towardServer) m_client = from;
                m_held.push_back({m_round + m_rounds, towardServer, bytes});
            }
        }

    private:
        UdpSocket            m_front;
        UdpSocket            m_back;
        NetAddress           m_server;
        NetAddress           m_client;
        std::deque<Held>     m_held;
        std::vector<uint8_t> m_lastToClient;
        int                  m_rounds = 0;
        int                  m_round  = 0;
        bool                 m_open   = false;
};
