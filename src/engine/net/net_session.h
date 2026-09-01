#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/component/core/transform.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/entity.h"
#include "net/prediction/command.h"
#include "net/transport/connection.h"
#include "net/prediction/interpolation.h"
#include "net/transport/reliable.h"
#include "net/prediction/rewind.h"
#include "net/wire/protocol.h"
#include "net/replication/snapshot.h"
#include "platform/net/udp_socket.h"

namespace Vkm::Engine {

class Scene;
class InputMap;
class ResourceManager;

/**
 * @brief How often the world goes out, in packets a second.
 *
 * Deliberately not "once a rendered frame". A machine drawing at 144 would
 * send 144 snapshots a second - at a kilobyte each, that is 140 KB/s per
 * player, five times what this costs at a rate anybody can see - and one
 * drawing at 20 would starve every client it has. Neither number has anything
 * to do with how often the world needs describing.
 *
 * Thirty-two is under the interpolation delay, so a client always has two
 * samples to draw between.
 */
constexpr float NET_SNAPSHOT_RATE = 64.0f;

/**
 * @brief How often input goes up, in packets a second.
 *
 * Higher than the snapshot rate, because a command packet is a tenth the size
 * and a tick the server has no command for is a tick the player did not get.
 */
constexpr float NET_COMMAND_RATE = 64.0f;

/**
 * @brief One end of a game, whether it is the authority or a guest.
 *
 * Owned by Engine by value, like the clock and the event bus, and reached
 * through FrameContext. It is not a System: it does not participate in the
 * frame's data flow, it brackets it - what arrived is applied before the ticks
 * that consume it, and what happened is sent after the ticks that caused it.
 *
 * Offline is a real state, not the absence of one. A single-player game has a
 * session in NetRole::Offline, and every question gameplay asks it - is this
 * mine, what is this entity's input this tick - has the answer it would have
 * had with no networking at all. That is what stops a project needing two code
 * paths, and it is why the editor can press Play on a networked game without a
 * server anywhere.
 */
class NetSession {
    public:
        /**
         * @brief What a joining player is given, decided by the game.
         *
         * The engine has no opinion about what a player *is* - a capsule, a
         * ship, a cursor - so a host that takes players supplies this and gets
         * back the entity that player owns. Returning a null entity refuses the
         * connection, which is how a game says "the match has started".
         */
        using SpawnPlayer = std::function<EntityId(Scene&, ResourceManager&, PlayerId)>;

        /**
         * @brief Undo what SpawnPlayer did.
         *
         * Called when a player leaves, times out, or the session closes.
         */
        using DespawnPlayer = std::function<void(Scene&, ResourceManager&, PlayerId, EntityId)>;

    public:
        NetSession();
        ~NetSession();

        NetSession(const NetSession& other) = delete;
        NetSession& operator=(const NetSession& other) = delete;

        NetSession(NetSession && other) = delete;
        NetSession& operator=(NetSession && other) = delete;

    public:
        /**
         * @brief Say which world this end loaded, so both can agree they match.
         *
         * A slot is an entity's name on the wire, so two ends whose scenes
         * differ agree on every name and mean different things by all of them -
         * a client on a stale scene joins successfully and is quietly wrong
         * about the whole world. The schema fingerprint catches a different
         * build; this catches the same build with a different world, which is
         * what a developer actually produces.
         *
         * Zero, the default, means "no file behind this world" and is compared
         * against nothing: a generated world neither offers nor demands
         * agreement.
         *
         * @param fingerprint What fingerprintScene said of the entry scene.
         */
        void setWorld(uint64_t fingerprint) { m_world = fingerprint; }

        /**
         * @brief Take players on @p port.
         *
         * A host referees and does not play. Somebody wanting to host and play
         * runs a server and joins it, which costs a second process and buys
         * something worth more than the process: every player in the game is a
         * real client, predicted, interpolated and corrected like every other.
         * A host that also played would have a character that is the authority
         * - never predicted, never corrected - so the configuration a developer
         * runs most would be exercising the client path for only half its
         * players.
         *
         * @param port       UDP port to bind. Zero takes whatever is free,
         *                   which is what the tests use.
         * @param maxPlayers Seats, the count project.json carries. A connection
         *                   past the last is refused with a reason rather than
         *                   ignored, and zero seats is a game nobody may join -
         *                   hosting one is refused rather than quietly served.
         * @return False when the port could not be bound.
         */
        bool host(uint16_t port, uint32_t maxPlayers);

        /**
         * @brief Join the game at @p server.
         *
         * Returns false only when the socket cannot be opened; being refused arrives
         * later, as a state change.
         */
        bool connect(const NetAddress& server);

        /// Say goodbye and go back to Offline. Safe to call when already offline.
        void close();

        /// What the game does when a player joins and leaves. Set before host().
        void onSpawn(SpawnPlayer spawn, DespawnPlayer despawn);

    public:
        NetRole  role() const { return m_role; }
        bool     isOffline() const { return m_role == NetRole::Offline; }

        /// True when a session was open and the other end has gone.
        bool     isDisconnected() const { return m_role == NetRole::Disconnected; }

        /**
         * @brief True once this end can play: always for a server, and for a client
         * from the Welcome until the connection ends.
         */
        bool isPlaying() const;

        /// Why the last attempt to join ended, or an empty string.
        const std::string& lastError() const { return m_lastError; }

        PlayerId localPlayer() const { return m_localPlayer; }

        /**
         * @brief The address this end is bound to.
         *
         * A host that asked for port zero finds out here which port it actually got,
         * which is what it shows a player who has to type it in.
         */
        NetAddress localAddress() const { return m_socket.localAddress(); }

        /**
         * @brief The entity this end drives, or a null id.
         *
         * On a server this is null: a server drives nobody and predicts nothing.
         */
        EntityId localEntity() const { return m_localEntity; }

        /// Round trip to the server, in seconds. Zero on a server or offline.
        float roundTrip() const;

        /**
         * @brief How many players this end knows of.
         *
         * Every seat on a server, and on a client its own and no other. What
         * crosses the wire is where the other players are, never who they are,
         * so a game that wants a scoreboard replicates a component of its own
         * keyed by PlayerId rather than asking here.
         *
         * @return Seats known to this end.
         */
        size_t playerCount() const;

    public:
        /**
         * @brief Whether this end decides what happens to @p entity.
         *
         * The one predicate everything else reads. A server says yes to
         * everything; a client says yes only to what it owns, because a body it
         * cannot correct is one it would shove its own way every tick and be
         * corrected on every frame - which is what props jittering on contact
         * actually is.
         *
         * Offline answers yes to everything, which is what leaves single-player
         * unchanged - and is also what a client becomes when its server stops
         * answering. A connection lost mid-game is a client that keeps playing,
         * now authoritative over a whole world nobody else can see; isPlaying()
         * is what a project asks to tell that apart, and a game that never asks
         * will not notice.
         */
        bool simulates(EntityId entity) const;

        /**
         * @brief Also simulate @p entities, for a while.
         *
         * Reported by physics each tick: the bodies this end's own character is
         * actually touching, and the bodies those are touching, out to the edge
         * of the pile. Without it a client predicts pushing a crate as pushing a
         * wall - the crate cannot move, so the character stops - and is then
         * corrected when the server says it did move. That is the softness felt
         * on every contact.
         *
         * A lease expires a fixed number of ticks after the last time it was
         * reported, rather than when contact breaks. A crate bouncing loses
         * contact for a tick at a time, and a lease that lapsed on each bounce
         * would hand the body back and forth; one that never lapsed would leave
         * a crate touched once simulated for the rest of the match.
         *
         * Refreshing an existing lease is what re-reporting does, so a caller
         * hands over the whole island every tick and nothing has to be removed.
         */
        void lease(const std::vector<EntityId>& entities);

        /**
         * @brief How many bodies this end is simulating on top of its own.
         *
         * For the status line, and for a test to see the island appear and lapse.
         */
        size_t leaseCount() const { return m_prediction.leases.size(); }

        /**
         * @brief Whether @p entity belongs to the player at this end.
         *
         * What a game asks to decide whether to draw a nameplate or read the local
         * camera.
         */
        bool isMine(EntityId entity) const;

        /**
         * @brief The input driving @p entity on the tick about to run.
         *
         * The seam that makes one character controller work in all three roles.
         * Offline and on the owning client it is the local command; on a server
         * it is the command that entity's player sent, taken from their buffer
         * and consumed once. An entity no player owns gets a zeroed command,
         * which reads as nothing held.
         */
        const InputCommand& commandFor(EntityId entity) const;

        /**
         * @brief Put every other player back to where @p shooter could see them.
         *
         * Reached through NetRewindScope rather than called directly. On a
         * server, every player's pose is restored to the moment this one was
         * drawing when the input being run was taken; offline and on a client
         * it does nothing, so the same gameplay code is a plain query in a
         * single-player game.
         *
         * The moment is not estimated from ping. A client already knows which
         * server tick it drew - its interpolation clock runs in the server's
         * own tick numbering - so it says so in its command packets, and what
         * arrives already accounts for that client's latency, its interpolation
         * delay and its frame time, with no clock to synchronise. It is a claim
         * from a client, so it is clamped to what is remembered.
         */
        void beginRewind(Scene& scene, EntityId shooter);

        /**
         * @brief Put the present back.
         *
         * Every rewound entity returns to exactly the transform it had, so nothing
         * outside the scope can tell.
         */
        void endRewind(Scene& scene);

    public:
        /**
         * @brief Apply everything that has arrived.
         *
         * Called once a frame, before the ticks that consume it. Takes the resource
         * manager because a join spawns a player, and what a player is made of is
         * content.
         */
        void receive(Scene& scene, ResourceManager& resources);

        /**
         * @brief The ticks a correction wants run again, oldest first.
         *
         * Empty except in the frame a snapshot disagreed with what this end
         * predicted. The caller drives them, because the session is not a
         * System and cannot run one.
         */
        const std::vector<InputCommand>& replayCommands() const { return m_prediction.replay; }

        /**
         * @brief Take @p command for a tick being run a second time.
         *
         * Deliberately not beginTick, which would queue the command for sending
         * again - it was sent when the tick first ran, and the server has
         * already answered for it.
         */
        void beginReplayTick(const InputCommand& command);

        /// Finish the replay and clear the request.
        void endReplay(Scene& scene);

        /**
         * @brief True while a tick that already happened is being run again.
         *
         * Read by the systems that must behave differently, and by nothing else.
         */
        bool replaying() const { return m_prediction.replaying; }

        /**
         * @brief Holds the outstanding correction in the scene for one scope.
         *
         * A replay puts the character exactly where the server says,
         * immediately - and a player would see their own character teleport. So
         * the simulation takes the correction at once and the picture takes it
         * over a handful of frames, which is what lets the correction afford to
         * be exact.
         *
         * The offset is a position chosen to look right, so a tick that started
         * from it would simulate something nothing decided. That makes putting
         * it in and taking it out a pair, and a pair written as two calls is a
         * rule the next edit between them can break silently. Written as a
         * scope, it cannot: construct it around the systems that read a
         * transform to draw it, and everything outside the braces sees where
         * the world actually is.
         */
        class Drawn {
            public:
                /**
                 * @brief Works a little of the correction off and applies the rest.
                 *
                 * @param session   The session holding what the picture owes.
                 * @param scene     The world about to be drawn.
                 * @param deltaTime The frame, in seconds. How much is worked off.
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
         * @brief Record what this end predicted for the tick that just ran.
         *
         * Called from inside the fixed step, after the systems have moved the
         * world. What is kept is compared against a later snapshot rather than
         * replaced by it - a snapshot describes a moment this end has already
         * moved past, so believing it outright is the jump backwards that
         * prediction exists to prevent.
         *
         * @param scene The world as this tick left it.
         * @param tick  The tick that ran.
         */
        void endTick(Scene& scene, uint32_t tick);

        /**
         * @brief Take the command this tick runs under.
         *
         * Called from inside the fixed step, after InputMap::beginTick built
         * the command. The action count comes with it because the wire
         * encoding is sized by it and both ends must agree - a game that
         * synthesises input rather than reading a device passes its own.
         *
         * @param tick        The tick about to run.
         * @param command     What the local player did on it.
         * @param actionCount How many action slots the project defines.
         */
        void beginTick(uint32_t tick, const InputCommand& command, uint32_t actionCount);

        /// Send what this frame produced. Called once a frame, after the ticks.
        void send(Scene& scene, uint32_t tick);

        /**
         * @brief Build @p prefab at @p at, here and on every client.
         *
         * The way a game creates something after the scene has loaded. On a
         * server the entity is built now and every client is told, reliably,
         * because a client that missed it would be told where an entity is
         * without ever having been told what it is, and would draw nothing.
         * Offline it is an ordinary instantiate.
         *
         * A client must not call this: what exists is the server's to decide,
         * and an entity built here alone would be corrected out of existence.
         *
         * @param scene     World to build into.
         * @param resources Resolves the prefab's asset names.
         * @param prefab    Project-relative prefab path.
         * @param at        Pose for the instance root.
         * @return The instance root, or a null id.
         */
        EntityId spawn(Scene& scene, ResourceManager& resources,
                       const std::string& prefab, const Transform& at);

        /**
         * @brief Destroy @p entity and its subtree, here and on every client.
         *
         * Snapshots already report an entity that has gone, so this exists for
         * the ordering rather than the fact: a client told to destroy an
         * instance root drops the subtree it built, which a slot report alone
         * would not describe.
         */
        void despawn(Scene& scene, EntityId entity);

        /**
         * @brief Wall-clock time passing, for the timers that must run while the
         * simulation does not - a paused editor still notices a peer leaving.
         */
        void advance(float seconds);

        /**
         * @brief Draw what this end is only told about where it was a moment ago.
         *
         * Called once a frame, on a client. A server and an offline session do
         * nothing here: they hold the present, so there is no past to draw.
         *
         * A body whose lease has just lapsed crosses from predicted to drawn a
         * few ticks behind, and the body did not move to do it - a player who
         * was pushing a crate a moment ago should not see it jump when they
         * stop. That step is worked off here like any other correction.
         *
         * @param scene     World to smooth.
         * @param deltaTime Real seconds since the last frame.
         * @param tickRate  Ticks a second.
         */
        void interpolate(Scene& scene, float deltaTime, float tickRate);

        /**
         * @brief How far behind the server this end is drawing, in ticks.
         *
         * Zero when this end holds the present.
         */
        float renderDelay() const;

        /**
         * @brief How far this end's prediction of its own entity was out, last time the
         * server said.
         *
         * Metres. Zero on a server and offline.
         */
        float predictionError() const { return m_prediction.error; }

    public:
        /**
         * @brief What one player looks like from the outside, for the editor's panel.
         *
         * A server sees every seat. A client sees its own and no other: what
         * crosses the wire is where the other players are, never who they are,
         * so a game that wants a scoreboard replicates a component of its own
         * keyed by PlayerId rather than asking here.
         */
        struct PlayerInfo {
            PlayerId id = NO_PLAYER;
            EntityId entity;
            float    roundTrip = 0.0f;
            bool     local = false;
        };

        std::vector<PlayerInfo> players() const;

        /// What this frame's snapshots cost, across every player. Empty on a client.
        const NetSnapshotStats& lastSnapshot() const { return m_lastSnapshot; }

        /**
         * @brief One line describing the session, for a log or a readout.
         *
         * What a person actually wants to know while playing: whether this end
         * is connected, who else is here, how far away they are, and whether
         * the prediction is holding. Built on demand, so a game that never asks
         * pays nothing.
         */
        std::string describe() const;

    private:
        struct Peer;
        struct DrawnFrom;

        void receiveAsServer(Scene& scene, ResourceManager& resources);

        /**
         * @brief Take what has arrived for a client, one datagram at a time.
         *
         * Dispatches to the handlers below. Three of them end the session, and
         * say so by answering false, because what is still waiting on the
         * socket belongs to a session that is over.
         */
        void receiveAsClient(Scene& scene, ResourceManager& resources);

        bool onWelcome(Scene& scene, BitReader& reader);
        bool onRefuse(BitReader& reader);
        bool onGoodbye();
        void onSnapshot(Scene& scene, ResourceManager& resources, BitReader& reader);

        /**
         * @brief Set aside what this end predicts, before a snapshot overwrites it.
         *
         * The character and every body it holds a lease on, except one taken
         * this very snapshot - that one is still drawn from the past, so what
         * is in the component is an interpolated pose rather than a prediction.
         * Where it was drawn goes to @p takenFrom instead, to be eased across.
         */
        void holdPredicted(Scene& scene, std::vector<DrawnFrom>& takenFrom);
        void applyMessage(Scene& scene, ResourceManager& resources,
                          const std::vector<uint8_t>& message);
        void broadcast(const uint8_t* bytes, size_t size);
        void sendAsServer(Scene& scene, uint32_t tick);
        void sendAsClient();

        void applyDrawCorrection(Scene& scene, float deltaTime);
        void removeDrawCorrection(Scene& scene);

        /**
         * @brief Drop everything naming an entity, because the world has gone.
         *
         * A replacement scene reuses the slot indices AND the generations of the
         * one it replaced, so an id from the old world compares alive against a
         * different entity in the new one - Scene::epoch() exists to say that
         * has happened. Nothing cached here can be checked against the new
         * world, so all of it goes.
         *
         * The seats are not cleared: which entity a player drives is the
         * project's answer, given by its spawn callback, and re-deciding it here
         * would be this session guessing at gameplay.
         */
        void forgetWorld();

        /**
         * @brief Judge this end's prediction against the server's answer for it.
         *
         * Either the prediction stands, or the ticks since the confirmed one
         * are queued to run again from what the server said. The snapshot has
         * already written that answer into the scene, so this reads the world
         * as the state a replay would start from.
         *
         * @param confirmedTick The newest tick of this end's input the server
         *                      has run.
         */
        void reconcile(Scene& scene, uint32_t confirmedTick);

        /**
         * @brief Put back what this end predicted, over what the snapshot wrote.
         *
         * Everything it predicts - the character and every body it is pushing -
         * because they are one answer: a crate it leans on decides where the
         * character ends up, so keeping one and discarding the other leaves the
         * two disagreeing about a collision they were both in.
         */
        void restoreHeld(Scene& scene);
        void correctDraw(EntityId entity, const glm::vec3& drawn, const glm::vec3& simulated);
        void offsetDrawn(Scene& scene, EntityId entity, const glm::vec3& by);
        glm::vec3 drawOffsetFor(EntityId entity) const;

        /// Build what the server spawned, unless this end already has it.
        void applySpawn(Scene& scene, ResourceManager& resources, BitReader& reader);

        /// Destroy what the server despawned, unless this end already has.
        void applyDespawn(Scene& scene, BitReader& reader);

        /**
         * @brief Build and send one peer their snapshot of this tick.
         *
         * @return What it cost, whether or not it was sent: an overflow is a
         *         fault worth reporting in the round's total rather than
         *         hiding as a peer that appears to have cost nothing.
         */
        NetSnapshotStats sendSnapshot(Scene& scene, const NetSchema& schema, Peer& peer,
                                      uint32_t tick);

        /**
         * @brief Is this Hello one this game can seat, and what does it define?
         *
         * Every way it can be no is a refusal with a reason, sent here, so a
         * caller has only to stop. @p actions is filled when the answer is yes.
         *
         * Every way it can be no is counted, and only the first says why.
         * Anyone can send a datagram, and a captured Hello can be replayed with
         * any field changed, so a line per refusal is a server's disk spent
         * from off the machine. The count rides the status line instead.
         */
        bool vetHello(const NetAddress& from, BitReader& reader, uint32_t& actions);

        /**
         * @brief Take one player's input packet: what they drew, and their commands.
         *
         * Every failure is the same answer - a packet that did not decode is
         * dropped and the next one is along in a tick.
         */
        void acceptCommands(Peer& peer, BitReader& reader);

        /**
         * @brief Take the tick a snapshot claims, unless it cannot be believed.
         *
         * @param serverTick The tick the snapshot's header names.
         * @return False if the snapshot is to be refused whole.
         */
        bool adoptServerTick(uint32_t serverTick);

        void greet(const NetAddress& from, const uint8_t* payload, size_t size,
                   Scene& scene, ResourceManager& resources);
        void refuse(const NetAddress& from, NetRefusal reason);
        void welcome(Peer& peer, const NetAddress& to);
        void dropPeer(Scene& scene, ResourceManager& resources, size_t index, const char* why);

        Peer* findPeer(const NetAddress& address);

        /**
         * @brief What this end predicted for its own entity, tick by tick.
         *
         * A client runs its own character at once, on its own input, without
         * waiting for the server to agree - because a character that waits is a
         * character that answers a keypress a round trip later, and that is the
         * one thing a player always notices.
         *
         * The server's answer arrives describing a tick long past, so it cannot
         * simply be applied: doing that is the yank backwards that prediction
         * exists to avoid. It is compared instead, against what this end
         * thought at that same tick, and only the difference is worth anything.
         */
        struct Predicted {
            uint32_t  tick = 0;
            glm::vec3 position{0.0f};
        };

        /**
         * @brief One body this end simulates because its character is touching it, and
         * for how many more ticks that stays true without being reported again.
         */
        struct Lease {
            EntityId entity;
            uint32_t ticks = 0;
        };

        /**
         * @brief What one body's picture owes, and is working off.
         *
         * Kept per body rather than for the character alone: a client predicts
         * whatever it is leaning on as well, and a crate that jumps under a
         * character that eases is the two of them visibly disagreeing about one
         * push. Lives here rather than in the component, so no tick ever
         * simulates from a position that was chosen to look nice.
         */
        struct DrawCorrection {
            EntityId  entity;
            glm::vec3 offset;  ///< Drawn minus simulated, still to work off.
        };

        /**
         * @brief One entity's prediction, kept across the reading of a snapshot
         *        that is about to describe an older moment for it.
         *
         * The snapshot goes in first so that "unchanged" means the server's word
         * rather than this end's guess; what was held is then either put back,
         * or left behind because a replay is about to recompute it.
         */
        struct Held {
            EntityId  entity;
            Transform transform;
            Rigidbody body;
            bool      hasBody = false;
        };

        /**
         * @brief Everything a client keeps in order to predict, and be corrected.
         *
         * One object because it is one job and one lifetime: a session that
         * ends or moves to another world drops all of it together, which is
         * what lets close(), connect() and forgetWorld() each say so in one
         * line rather than keep three lists that have to agree.
         *
         * A server holds one and never fills it: it predicts nothing, because
         * it is what predictions are compared against.
         */
        struct Prediction {
            std::vector<Predicted>      predicted;   ///< Where its own entity was, per tick.
            float                       error = 0.0f;
            std::vector<InputCommand>   unacknowledged;
            std::vector<InputCommand>   replay;      ///< The ticks a replay is re-running.
            bool                        replaying = false;

            /// The bodies this end decides beyond its own: what it holds, what
            /// it took this snapshot, and what it has just given back.
            std::vector<Lease>          leases;
            std::vector<EntityId>       acquired;
            std::vector<EntityId>       released;

            /// What the player last saw, and how far the picture still owes.
            std::vector<Held>           held;
            std::vector<DrawCorrection> corrections;

            bool warnedNoReplay = false;  ///< Said once when the link outruns a replay.

            void clear() { *this = Prediction{}; }
        };

        /// Where a body this end has just taken over was last drawn, read
        /// before the snapshot overwrites it.
        struct DrawnFrom {
            EntityId  entity;
            glm::vec3 drawn{0.0f};
        };

        /// What beginRewind moved, so endRewind can put it back exactly.
        struct Rewound {
            EntityId  entity;
            Transform transform;
        };

    private:
        UdpSocket m_socket;
        NetRole   m_role = NetRole::Offline;

        std::vector<std::unique_ptr<Peer>> m_peers;   ///< Server side.
        std::unique_ptr<NetConnection>     m_server;  ///< Client side.

        SpawnPlayer   m_spawn;
        DespawnPlayer m_despawn;

        uint32_t m_maxPlayers  = 8;
        PlayerId m_nextPlayer  = 1;
        PlayerId m_localPlayer = NO_PLAYER;
        EntityId m_localEntity;

        InputCommand              m_currentCommand;
        uint32_t                  m_actionCount = 0;
        bool                      m_ticked      = false;  ///< A tick has run, so the count is real.

        std::string      m_lastError;
        NetSnapshotStats m_lastSnapshot;

        float m_sendAccumulator = 1.0f;  ///< Real seconds since anything was sent.

        NetReliable m_reliable;  ///< Carries this end's acknowledgement upstream.
        NetRewind   m_rewind;    ///< Where each player has been, for judging a shot.

        /**
         * @brief The authority's tick, as this end knows it.
         *
         * Its own count on a server, and on a client the newest one a snapshot
         * header carried. One meaning either way, which the name states and the
         * two ends reach differently - a client cannot know it except by being
         * told, because the two clocks were never related: each started when
         * its own process did. Written from the local clock on a client, it
         * would name this end's tick everywhere the authority's is meant.
         */
        uint32_t              m_serverTick = 0;

        bool                  m_warnedTickJump = false;  ///< Said once, not once a packet.

        /// This end's own prediction, and everything it is kept in step with.
        Prediction m_prediction;

        std::vector<Rewound>  m_rewound;

        NetInterpolation      m_interpolation;
        std::vector<uint32_t> m_touched;
        uint64_t              m_worldEpoch = 0;  ///< Which world the ids above name.

        uint64_t m_world = 0;  ///< Which world this end loaded, or zero for none.
        uint64_t m_token = 0;  ///< Client side: what proves this end is the seat's owner.

        uint32_t m_refusedMismatch = 0;  ///< Joins refused for a different build.
        uint32_t m_refusedFull     = 0;  ///< Joins refused because every seat was taken.

        std::vector<uint8_t>  m_scratch;
        std::vector<uint8_t>  m_datagram;
        std::vector<uint16_t> m_acknowledged;
};

} // namespace Vkm::Engine
