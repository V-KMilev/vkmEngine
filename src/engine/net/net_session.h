#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ecs/component/core/transform.h"
#include "ecs/entity.h"
#include "net/net_client.h"
#include "net/net_server.h"
#include "net/prediction/command.h"
#include "net/transport/connection.h"
#include "net/wire/bit_stream.h"
#include "net/wire/protocol.h"
#include "platform/net/udp_socket.h"

namespace Vkm::Engine {

class Clock;
class EventBus;
class Scene;
class ResourceManager;

/**
 * @brief How often the world goes out, in packets a second, independent of frame rate.
 *
 * Decides how late another player looks: the interpolation delay is counted in
 * snapshots, two of them 31 ms here. Measurements are in docs/reference/networking.md.
 */
constexpr float NET_SNAPSHOT_RATE = 64.0f;

/**
 * @brief How often input goes up, in packets a second.
 *
 * Decides how stale the server's picture of a player's input may be; a tick
 * the server has no command for is a tick the player did not get.
 */
constexpr float NET_COMMAND_RATE = 64.0f;

/**
 * @brief The fastest tick rate a networked session runs at: two commands to a
 *        command packet.
 *
 * NetCommandBuffer::MAX_DEPTH, NET_COMMAND_REDUNDANCY and the pacing cushion
 * are counted in commands and sized for this; faster, one packet fills the
 * queue up to the trim. host() and connect() refuse a faster rate.
 */
constexpr uint32_t NET_MAX_TICK_RATE = static_cast<uint32_t>(2.0f * NET_COMMAND_RATE);

/**
 * @brief What a session's server and client halves share: the socket, what the two
 *        ends agreed, and the buffers a datagram is built in.
 */
struct NetCore {
    /**
     * @brief Datagrams read in one frame.
     *
     * Draining until empty would make the frame's cost the sender's choice. What
     * is left waits.
     */
    static constexpr size_t MAX_READS_PER_FRAME = 512;

    UdpSocket socket;
    uint32_t  tickRate = 0;  ///< Fixed for the session.

    uint32_t actionCount = 0;

    /// Which action has which slot: this end's own, or on a server whose behaviors
    /// have defined none yet, its first player's.
    uint64_t actionNames = 0;
    bool     ticked      = false;  ///< A tick has run, so the count is real.

    uint64_t world = 0;  ///< Which world this end loaded, or zero for none.

    /// The authority's tick: its own count on a server; on a client, the newest a
    /// snapshot carried, since the two clocks are unrelated.
    uint32_t serverTick = 0;

    std::string lastError;

    std::vector<uint8_t>  scratch;
    std::vector<uint8_t>  payload;   ///< What an outgoing message is written into.
    std::vector<uint8_t>  datagram;  ///< The datagram last received, or last framed to send.
    std::vector<uint16_t> acknowledged;
};

/**
 * @brief Send @p message, which carries nothing past its header, on @p connection.
 *
 * @param core       Its socket sends, and its buffers build the datagram.
 * @param connection Frames it, and names where it goes.
 * @param message    What it is: a Goodbye or a Challenge.
 * @param token      The token it carries.
 */
void sendBare(NetCore& core, NetConnection& connection, NetMessage message, uint64_t token);

/**
 * @brief The payload of the datagram last received, past the connection's header.
 *
 * Empty for a datagram too short to carry that header, so its message header
 * reads as NetSender::Unknown.
 *
 * @param core Holds the datagram last received.
 * @return A reader over the payload.
 */
BitReader receivedPayload(const NetCore& core);

/**
 * @brief One end of a game, whether it is the authority or a guest.
 *
 * Not a System: it brackets the frame - what arrived is applied before the
 * ticks that consume it, what happened is sent after them.
 *
 * Offline is a real state: a single-player game's session answers every
 * question as it would with no networking at all.
 */
class NetSession {
    public:
        /**
         * @brief Holds the outstanding correction in the scene for one scope.
         *
         * The simulation takes a correction at once; the picture takes it over
         * a few frames. The offset is a position chosen to look right, so no
         * tick may start from it: construct this only around the systems that
         * read a transform to draw it.
         */
        class Drawn {
            public:
                /**
                 * @brief Works a little of the correction off and applies the rest.
                 *
                 * @param session   Holds what the picture owes.
                 * @param scene     The world about to be drawn.
                 * @param deltaTime Frame seconds; how much is worked off.
                 */
                Drawn(NetSession& session, Scene& scene, float deltaTime);
                ~Drawn();

                Drawn(const Drawn& other) = delete;
                Drawn& operator=(const Drawn& other) = delete;

                Drawn(Drawn && other) = delete;
                Drawn& operator=(Drawn && other) = delete;

            private:
                NetSession& m_session;
                Scene&      m_scene;
        };

        /**
         * @brief Puts every other player back where a shooter saw them, for one scope.
         *
         * A scope rather than a wrapped query, so it serves any query; offline and
         * on a client it does nothing. Read inside the braces, act outside: what was
         * moved is restored exactly on the way out, discarding anything written to it.
         */
        class Rewind {
            public:
                /**
                 * @brief Rewinds to the moment @p shooter was drawing when the running
                 *        command was made.
                 *
                 * That is the server tick the client says it drew, less how long the
                 * command waited in the queue, clamped to what is remembered. A scope
                 * nested in another changes nothing and must name the same shooter.
                 *
                 * @param session Whose players are rewound.
                 * @param scene   The world to rewind.
                 * @param shooter Whose moment is restored; it stays where it is.
                 */
                Rewind(NetSession& session, Scene& scene, EntityId shooter);
                ~Rewind();

                Rewind(const Rewind& other) = delete;
                Rewind& operator=(const Rewind& other) = delete;

                Rewind(Rewind && other) = delete;
                Rewind& operator=(Rewind && other) = delete;

            private:
                NetSession& m_session;
                Scene&      m_scene;
        };

    public:
        NetSession();
        ~NetSession();

        NetSession(const NetSession& other) = delete;
        NetSession& operator=(const NetSession& other) = delete;

        NetSession(NetSession && other) = delete;
        NetSession& operator=(NetSession && other) = delete;

    public:
        /**
         * @brief Say which world this end loaded, as loaded, before anything in it has run.
         *
         * Slots name entities on the wire, so different scenes would agree on
         * every name and mean different things: the fingerprint is compared at a join,
         * and zero - no file behind this world - against nothing. What @p scene holds
         * now is what both ends share; a client names those entities by their own
         * slots, and makes whatever else the server names where it has room.
         *
         * @param fingerprint What fingerprintScene said of the entry scene.
         * @param scene       The world, just opened.
         */
        void setWorld(uint64_t fingerprint, const Scene& scene);

        /**
         * @brief Take players on @p port.
         *
         * A host referees and does not play; to play too, run a server and join it.
         *
         * @param port       UDP port to bind; zero takes whatever is free.
         * @param maxPlayers Seats; a connection past the last is refused, and
         *                   zero is refused.
         * @param tickRate   The clock's ticks a second; at most NET_MAX_TICK_RATE.
         * @return False when the port could not be bound, @p maxPlayers is
         *         zero, or @p tickRate is too fast.
         */
        bool host(uint16_t port, uint32_t maxPlayers, uint32_t tickRate);

        /**
         * @brief Join the game at @p server.
         *
         * @param server   Where the game is.
         * @param tickRate The clock's ticks a second; at most NET_MAX_TICK_RATE.
         * @return False when the socket cannot be opened or @p tickRate is too
         *         fast. A refusal arrives later, as a state change.
         */
        bool connect(const NetAddress& server, uint32_t tickRate);

        /**
         * @brief Say goodbye and go back to Offline. Safe to call when already offline.
         *
         * What the session made goes at the next receive().
         */
        void close();

        /// What the game does when a player joins and leaves. Set before host().
        void onSpawn(NetServer::SpawnPlayer spawn, NetServer::DespawnPlayer despawn);

    public:
        NetRole  role() const { return m_role; }
        bool     isOffline() const { return m_role == NetRole::Offline; }

        /// True when a session was open and the other end has gone.
        bool     isDisconnected() const { return m_role == NetRole::Disconnected; }

        /**
         * @brief True once this end can play: always for a server, and for a client
         *        from the Welcome until the connection ends.
         *
         * @return Whether this end can play.
         */
        bool isPlaying() const;

        /// Why the last attempt to join ended, or an empty string.
        const std::string& lastError() const { return m_core.lastError; }

        PlayerId localPlayer() const { return m_client.localPlayer(); }

        /**
         * @brief The address this end is bound to; the real port after hosting on zero.
         *
         * @return The bound address.
         */
        NetAddress localAddress() const { return m_core.socket.localAddress(); }

        /**
         * @brief The entity this end drives; null on a server.
         *
         * @return The entity, or a null id.
         */
        EntityId localEntity() const { return m_client.localEntity(); }

        /// Round trip to the server, in seconds. Zero on a server or offline.
        float roundTrip() const;

        /**
         * @brief How many players this end knows of.
         *
         * Every seat on a server; only its own on a client, since who the
         * others are never crosses the wire - a scoreboard replicates its own
         * component keyed by PlayerId.
         *
         * @return Seats known to this end.
         */
        size_t playerCount() const;

    public:
        /**
         * @brief Whether this end decides what happens to @p entity.
         *
         * A server says yes to everything, offline too. A client says yes to the character it
         * plays and what it leases, and, outside a replay, to what no slot of the server's names:
         * its own entities, and those at or below a NetLocal. A replay re-runs only what the
         * client predicts. A client that loses or is refused by its server ends Disconnected,
         * and answers no for the server's world it holds still. Ask the role, not isPlaying(),
         * which is false for both Disconnected and Offline.
         *
         * @param scene  The world @p entity is in.
         * @param entity The entity asked about; a null id is never simulated by a client.
         * @return Whether this end decides what happens to @p entity.
         */
        bool simulates(const Scene& scene, EntityId entity) const;

        /**
         * @brief Also simulate @p entities, for a while.
         *
         * For the pile this end's character is touching (see
         * PhysicsSystem::leaseContacts); without it a pushed crate is predicted
         * as a wall. A lease lapses a fixed number of ticks after its last
         * report, not when contact breaks, so a bouncing body is not handed
         * back and forth. Re-reporting refreshes, so pass the whole island
         * every tick.
         *
         * @param entities Bodies to simulate; null ids and this end's own
         *                 entity are skipped.
         */
        void lease(const std::vector<EntityId>& entities) { m_client.lease(entities); }

        /**
         * @brief Send the server something it must have: a move, a name, a choice in a lobby.
         *
         * Delivered at most once and in the order sent, as a PlayerMessageEvent at the server's
         * next event flush; what is still on its way when this end leaves goes with it. It rides
         * every packet this end sends until the server says it has it, so a lost datagram costs a
         * packet interval, not the message. Input for a tick is not a message: the command
         * carries that, and setPayload what is held beside it. Offline there is no server to
         * tell: the game is its own authority and applies the choice where it makes it.
         *
         * @param bytes The message; the game's own bytes.
         * @param size  1 to NET_MESSAGE_MAX_BYTES of them.
         * @return False, sending nothing, unless this end is a client playing, the message
         *         fits, and fewer than NET_MESSAGE_WINDOW are still unacknowledged.
         */
        bool sendMessage(const uint8_t* bytes, size_t size) {
            return m_role == NetRole::Client && m_client.sendMessage(bytes, size);
        }

        /**
         * @brief How many bodies this end is simulating on top of its own.
         *
         * @return The number of live leases.
         */
        size_t leaseCount() const { return m_client.leaseCount(); }

        /**
         * @brief Whether @p entity belongs to the player at this end.
         *
         * @param entity The entity asked about.
         * @return True for everything offline, for this end's own entity on a
         *         client, and for nothing on a server.
         */
        bool isMine(EntityId entity) const;

        /**
         * @brief What the server's @p slot names on this end.
         *
         * An entity's name on the wire is the server's slot. A client makes what the server
         * names where it has room, so a replicated component that names an entity sends the
         * server's slot, and a client looks it up here. On a server or offline it is the slot.
         *
         * @param scene The world it is in.
         * @param slot  The server's slot.
         * @return The entity, or a null id when this end has nothing for it.
         */
        EntityId entityFor(const Scene& scene, uint32_t slot) const;

        /**
         * @brief The input driving @p entity on the tick about to run.
         *
         * Offline and on the owning client, the local command; on a server, the
         * one that entity's player sent, consumed once. An entity no player
         * owns gets a zeroed command.
         *
         * @param entity The entity about to be driven.
         * @return The command it runs under this tick.
         */
        const InputCommand& commandFor(EntityId entity) const;

    public:
        /**
         * @brief Apply everything that has arrived.
         *
         * Once a frame, before the ticks. A join spawns a player on a server and
         * a client builds spawned prefabs, hence the resource manager.
         *
         * @param scene     The world what arrived is applied to.
         * @param resources Loads a spawned player's or prefab's assets.
         */
        void receive(Scene& scene, ResourceManager& resources);

        /**
         * @brief Hand what players sent since the last call to @p events, as PlayerMessageEvent.
         *
         * Once a frame, after receive. Only a server has any.
         *
         * @param events Where they are queued, for the next flush.
         */
        void deliver(EventBus& events);

        /**
         * @brief The ticks a correction wants run again, oldest first.
         *
         * Empty except in the frame a snapshot disagreed with the prediction.
         * The caller drives them; the session cannot run a System.
         *
         * @return The commands to run again, oldest first.
         */
        const std::vector<InputCommand>& replayCommands() const { return m_client.replayCommands(); }

        /**
         * @brief Take @p command for a tick being run a second time.
         *
         * Not beginTick, which would queue it for sending again.
         *
         * @param command The command that tick first ran under, from replayCommands().
         */
        void beginReplayTick(const InputCommand& command);

        /// Finish the replay and clear the request.
        void endReplay(Scene& scene) { m_client.endReplay(scene); }

        /**
         * @brief True while a tick that already happened is being run again.
         *
         * @return Whether a replay is in progress.
         */
        bool replaying() const { return m_client.replaying(); }

        /**
         * @brief Record what this end predicted for the tick that just ran.
         *
         * Inside the fixed step, after the systems. Kept to compare against a
         * later snapshot, which describes a moment this end has moved past.
         *
         * @param scene The world as this tick left it.
         * @param tick  The tick that ran.
         */
        void endTick(Scene& scene, uint32_t tick);

        /**
         * @brief Take the command this tick runs under.
         *
         * Inside the fixed step, after InputMap::beginTick. Both ends must agree
         * on the action count (it sizes the encoding) and on which action has
         * which slot (the fingerprint).
         *
         * @param tick        The tick about to run.
         * @param command     What the local player did on it.
         * @param actionCount Action slots the project defines.
         * @param actionNames InputMap::actionFingerprint, or zero to be compared
         *                    against nothing.
         */
        void beginTick(
            uint32_t tick,
            const InputCommand& command,
            uint32_t actionCount,
            uint64_t actionNames = 0
        );

        /// Send what this frame produced. Called once a frame, after the ticks.
        void send(Scene& scene, uint32_t tick);

        /**
         * @brief Build @p prefab at @p at, here and on every client.
         *
         * On a server the root gets a NetSpawn, which replicates so every
         * client, and every later joiner, builds the same file into it. Offline
         * it is an ordinary instantiate. Undone by destroying the root's subtree
         * (HierarchyOperations::destroyHierarchy or Behavior::destroy). Authority
         * only: refused on a client, as the server would never hear of what it built;
         * a client's own entity is an ordinary Prefab::instantiate.
         *
         * @param scene     World to build into.
         * @param resources Resolves the prefab's asset names.
         * @param prefab    Project-relative path; on a server, one that names
         *                  nothing outside the project - see isSayable.
         * @param at        Root pose; on a server, finite, with a rotation of
         *                  some length and a bounded scale.
         * @return The instance root, or a null id when nothing was built.
         */
        EntityId spawn(
            Scene& scene,
            ResourceManager& resources,
            const std::string& prefab,
            const Transform& at
        );

        /**
         * @brief Wall-clock time passing, for the timers that must run while the
         *        simulation does not - a paused editor still notices a peer leaving.
         *
         * @param seconds Real seconds since the last call.
         */
        void advance(float seconds);

        /**
         * @brief Draw what this end is only told about where it was a moment ago.
         *
         * Once a frame; a no-op except on a client. Also works off the step a
         * body takes when its lease lapses and it drops back to being drawn.
         *
         * @param scene     World to smooth.
         * @param deltaTime Real seconds since the last frame.
         */
        void interpolate(Scene& scene, float deltaTime);

        /**
         * @brief How far behind the server this end is drawing, in ticks; zero
         *        when it holds the present.
         *
         * @return The delay, in ticks.
         */
        float renderDelay() const;

        /**
         * @brief The seconds of the authority's world this end is drawing.
         *
         * What moves by the clock and must look alike at every end is moved by these. Offline and
         * on a server, the clock's ticks and the fraction past the last, so it stops with the
         * game, and inside a tick that tick's own, getTick() * getFixedStep(), so what a tick
         * decides by it it decides the same way each time. On a playing client, from the first
         * snapshot on, the server tick it draws others at, so what the server floats is drawn on
         * the sea it floated on; the two clocks are otherwise unrelated, and a client that has
         * lost its server goes back to its own.
         *
         * @param clock The engine's clock.
         * @return Seconds of the authority's simulated time.
         */
        double worldTime(const Clock& clock) const;

        /**
         * @brief How much faster than the wall clock this end should run its ticks.
         *
         * A playing client's answer from NetPacing; exactly one in every other
         * role and state. See Clock::setPacing.
         *
         * @return The rate scale; one outside a playing client.
         */
        float pacing() const;

        /**
         * @brief How far this end's prediction of its own entity was out, last time the
         *        server said.
         *
         * @return The error in metres; zero on a server and offline.
         */
        float predictionError() const { return m_client.predictionError(); }

    public:
        /**
         * @brief One line describing the session, for a log or a readout.
         *
         * Connection, peers, their distance and prediction health; built on demand.
         *
         * @return The line.
         */
        std::string describe() const;

    private:
        static constexpr uint64_t NO_WORLD = UINT64_MAX;  ///< No world's epoch seen yet.

    private:
        /**
         * @brief Whether a session can run at @p tickRate, saying why not when it cannot.
         *
         * @param tickRate Ticks a second the clock was given.
         * @return False, having logged and set lastError(), past NET_MAX_TICK_RATE or at zero.
         */
        bool acceptsTickRate(uint32_t tickRate);

        /**
         * @brief End a client's session as NetRole::Disconnected rather than Offline.
         *
         * So a client that did not get to play never plays alone instead, and the world it was
         * shown holds still - what the session made stays until close() or the next connect().
         */
        void disconnect();

        /// Close the link, whichever half held it, and reset what a session counts.
        void hangUp();

        /**
         * @brief Drop everything naming an entity, because the world has gone.
         *
         * A replacement scene reuses slots and generations, so old ids compare
         * alive (Scene::epoch() says when). The seats stay: which entity a
         * player drives is the spawn callback's answer.
         */
        void forgetWorld();

        void beginRewind(Scene& scene, EntityId shooter);
        void endRewind(Scene& scene);

    private:
        NetCore   m_core;
        NetServer m_server;
        NetClient m_client;
        NetRole   m_role = NetRole::Offline;

        /// What the local player does this tick, offline and on a client.
        InputCommand m_currentCommand;

        /// Real seconds owed toward the next send; starts full so the first goes at once.
        float m_sendAccumulator = 1.0f;
        bool  m_tickOpen        = false;  ///< Between beginTick and endTick: worldTime is the tick's.

        uint32_t m_rewindDepth = 0;  ///< Open scopes; only the outermost moves anything.
        EntityId m_rewoundFor;       ///< Whose moment the open scope is.

        /// Which world the halves' entity ids name; none seen yet is its own value, as a world
        /// built in code and never loaded is epoch zero.
        uint64_t m_worldEpoch = NO_WORLD;
};

} // namespace Vkm::Engine
