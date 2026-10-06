#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ecs/component/core/transform.h"
#include "ecs/entity.h"
#include "net/prediction/command.h"
#include "net/prediction/rewind.h"
#include "net/replication/silence.h"
#include "net/replication/snapshot.h"
#include "net/transport/connection.h"
#include "net/transport/join_cookie.h"
#include "net/wire/bit_stream.h"
#include "net/wire/protocol.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;
struct NetCore;

/**
 * @brief The authority's half of a NetSession: seats, the join handshake,
 *        commands in and snapshots out.
 *
 * Holds nothing outside a server's session; NetSession decides when it is asked.
 */
class NetServer {
    public:
        /**
         * @brief What a joining player is given, decided by the game.
         *
         * Returns the entity that player owns; a null entity refuses the connection.
         */
        using SpawnPlayer = std::function<EntityId(Scene&, ResourceManager&, PlayerId)>;

        /**
         * @brief Undo what SpawnPlayer did.
         *
         * Called when a player leaves, times out, or the session closes.
         */
        using DespawnPlayer = std::function<void(Scene&, ResourceManager&, PlayerId, EntityId)>;

    public:
        explicit NetServer(NetCore& core);
        ~NetServer();

        NetServer(const NetServer& other) = delete;
        NetServer& operator=(const NetServer& other) = delete;

        NetServer(NetServer && other) = delete;
        NetServer& operator=(NetServer && other) = delete;

    public:
        void onSpawn(SpawnPlayer spawn, DespawnPlayer despawn);

        /// Start taking up to @p maxPlayers players, under a fresh join key.
        void open(uint32_t maxPlayers);

        /// Tell every player goodbye and forget them.
        void close();

        void advance(float seconds);

        /// Take every datagram that has arrived, then give back the seats that are going.
        void receive(Scene& scene, ResourceManager& resources);

        /// Hand every player the command its entity runs this tick.
        void beginTick(uint32_t tick);

        /// Remember where every player is, for judging a shot later.
        void endTick(const Scene& scene, uint32_t tick);

        /// Send every player who has answered its snapshot of this tick.
        void send(Scene& scene, uint32_t tick);

        /**
         * @brief The command @p entity's player sent for this tick.
         *
         * @param entity The entity about to be driven.
         * @return The command, or null when no player drives @p entity.
         */
        const InputCommand* commandFor(EntityId entity) const;

        /**
         * @brief Put every player but @p shooter where @p shooter saw them.
         *
         * @param scene   The world to rewind.
         * @param shooter Whose moment is restored; a shooter no player drives is
         *                judged against the present.
         */
        void rewind(Scene& scene, EntityId shooter);

        /// Put back exactly what rewind moved.
        void restore(Scene& scene);

        /// Drop everything naming an entity; see NetSession::forgetWorld.
        void forgetWorld();

        size_t playerCount() const { return m_peers.size(); }

        std::string describe() const;

    private:
        struct Peer;

        /// What rewind moved, so restore can put it back exactly.
        struct Rewound {
            EntityId  entity;
            Transform transform;
        };

    private:
        /**
         * @brief Build and send one peer their snapshot of this tick.
         *
         * @param scene  The world to describe.
         * @param schema What replicates.
         * @param peer   Recipient; its baseline and command queue are read.
         * @param tick   The server tick the snapshot carries.
         * @return What it cost, whether or not it was sent.
         */
        NetSnapshotStats sendSnapshot(Scene& scene, const NetSchema& schema, Peer& peer, uint32_t tick);

        /**
         * @brief Bring every Transform this round describes to the scale the wire carries.
         *
         * A client holds netScale of what it was told, so the server must too.
         * A silenced body is the scene file's at both ends already. Logged once.
         *
         * @param scene The world about to be described.
         */
        void holdScalesToTheWire(Scene& scene);

        /**
         * @brief Is this Hello one this game can seat, and what does it define?
         *
         * An undecodable Hello is dropped; every other no is a refusal sent
         * here. Refusals are counted, for describe(), and only the first
         * mismatch is logged: anyone can send a datagram.
         *
         * @param from        Where the Hello came from; a refusal goes back there.
         * @param echoed      The Hello's token, carried back in a refusal so the
         *                    client can tell it from a forgery.
         * @param reader      The Hello, past its message header.
         * @param actions     Filled with the action count it defines, on yes.
         * @param actionNames Filled with the fingerprint of its action slots.
         * @return True when the Hello can be seated.
         */
        bool vetHello(
            const NetAddress& from,
            uint64_t echoed,
            BitReader& reader,
            uint32_t& actions,
            uint64_t& actionNames
        );

        /**
         * @brief Take one player's input packet: what they drew, and their commands.
         *
         * A packet that did not decode is dropped; the next is a tick away.
         *
         * @param peer   The player who sent it.
         * @param reader The packet, past its message header.
         */
        void acceptCommands(Peer& peer, BitReader& reader);

        /**
         * @brief Answer a Hello from an address that holds no seat.
         *
         * Seated only if it passes vetHello and echoes this address's token,
         * proving it is not a forged source - only then does SpawnPlayer run.
         * Otherwise it gets the token in a Challenge and nothing is kept.
         *
         * @param from      Where the Hello came from.
         * @param echoed    The token the Hello carried; zero for none.
         * @param hello     The Hello, past its message header.
         * @param scene     The world a seated player is spawned into.
         * @param resources Handed to the game's SpawnPlayer.
         */
        void greet(
            const NetAddress& from,
            uint64_t echoed,
            BitReader& hello,
            Scene& scene,
            ResourceManager& resources
        );

        void challenge(const NetAddress& to);
        void refuse(const NetAddress& from, NetRefusal reason, uint64_t token);
        void welcome(Peer& peer, const NetAddress& to);

        /**
         * @brief The number the next seated player takes, or NO_PLAYER if none is free.
         *
         * Counts on from the last one taken, skipping NO_PLAYER and held numbers.
         *
         * @return The number, or NO_PLAYER.
         */
        PlayerId freePlayerId() const;
        void dropPeer(Scene& scene, ResourceManager& resources, size_t index, const char* why);

        Peer* findPeer(const NetAddress& address);

    private:
        NetCore& m_core;

        std::vector<std::unique_ptr<Peer>> m_peers;

        SpawnPlayer   m_spawn;
        DespawnPlayer m_despawn;

        uint32_t m_maxPlayers = 0;  ///< What open() was given.
        PlayerId m_nextPlayer = 1;

        NetSnapshotStats m_lastSnapshot;
        bool             m_warnedScale = false;  ///< Said once, not once a round.

        NetRewind            m_rewind;  ///< Where each player has been, for judging a shot.
        std::vector<Rewound> m_rewound;

        NetSilenceMap m_silence;  ///< Rebuilt once per send round.

        NetJoinCookie m_cookie;         ///< The token each address is told.
        double        m_seconds = 0.0;  ///< Real time advance() has passed, for the token's window.

        uint32_t m_refusedMismatch = 0;  ///< Joins refused for a different build.
        uint32_t m_refusedFull     = 0;  ///< Joins refused because every seat was taken.

        std::vector<InputCommand> m_received;  ///< One command packet, decoded.

        /// Frames a reply to an address that holds no seat.
        NetConnection m_unseated;
};

} // namespace Vkm::Engine
