#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/component/core/transform.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/entity.h"
#include "net/prediction/command.h"
#include "net/prediction/interpolation.h"
#include "net/prediction/pacing.h"
#include "net/transport/connection.h"
#include "net/wire/bit_stream.h"
#include "net/wire/protocol.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;
struct NetCore;
struct NetSpawn;

/**
 * @brief Spawned prefabs a client builds in one frame.
 *
 * Each build reads a file and the server decides how many arrive; the rest
 * wait for the next frame, so a burst costs a few frames instead of stalling one.
 */
constexpr uint32_t NET_SPAWN_BUILDS_PER_FRAME = 8;

/**
 * @brief A guest's half of a NetSession: joining, predicting what it owns,
 *        being corrected, and drawing the rest late.
 *
 * Holds nothing outside a client's session; NetSession decides when it is asked.
 */
class NetClient {
    public:
        explicit NetClient(NetCore& core);
        ~NetClient();

        NetClient(const NetClient& other) = delete;
        NetClient& operator=(const NetClient& other) = delete;

        NetClient(NetClient && other) = delete;
        NetClient& operator=(NetClient && other) = delete;

    public:
        /// Start talking to @p server. Nothing is sent until the first tick has run.
        void open(const NetAddress& server);

        /// Tell the server goodbye and forget everything this session held.
        void close();

        void advance(float seconds);

        /**
         * @brief Take what has arrived, one datagram at a time, then build what was spawned.
         *
         * @param scene     The world a Welcome or a snapshot is applied to.
         * @param resources Builds the prefabs the server spawned.
         * @return False when the session has ended: refused, told goodbye, or
         *         timed out. What is still on the socket belongs to a session that is over.
         */
        bool receive(Scene& scene, ResourceManager& resources);

        /// Send a Hello until welcomed, then the commands the server has not heard.
        void send();

        /// Whether this end decides @p entity: its own, or one it leases.
        bool simulates(EntityId entity) const;

        /// See NetSession::lease.
        void lease(const std::vector<EntityId>& entities);

        /// Count every lease down a tick, outside a replay, and hand back the lapsed.
        void ageLeases();

        /// Keep @p command until the server says it ran it.
        void beginTick(const InputCommand& command);

        /// Record where this end's own entity ended @p tick.
        void endTick(const Scene& scene, uint32_t tick);

        void beginReplayTick() { m_prediction.replaying = true; }

        /// Finish the replay and measure what the picture owes for it.
        void endReplay(const Scene& scene);

        void interpolate(Scene& scene, float deltaTime);

        /// Work a little of every outstanding correction off, and apply the rest.
        void applyDrawCorrection(Scene& scene, float deltaTime);

        /// Take back what applyDrawCorrection applied.
        void removeDrawCorrection(Scene& scene);

        /// Drop everything naming an entity; see NetSession::forgetWorld.
        void forgetWorld();

        std::string describe() const;

    public:
        PlayerId localPlayer() const { return m_localPlayer; }
        EntityId localEntity() const { return m_localEntity; }

        /// From the Welcome until the session ends.
        bool isPlaying() const { return m_localPlayer != NO_PLAYER; }

        float roundTrip() const { return m_connection ? m_connection->roundTrip() : 0.0f; }

        size_t leaseCount() const { return m_prediction.leases.size(); }

        const std::vector<InputCommand>& replayCommands() const { return m_prediction.replay; }

        bool replaying() const { return m_prediction.replaying; }

        float predictionError() const { return m_prediction.error; }

        float renderDelay() const { return m_interpolation.behindTicks(); }

        float pacing() const { return m_prediction.pacing.scale(); }

    private:
        /**
         * @brief What this end predicted for its own entity, tick by tick.
         */
        struct Predicted {
            uint32_t  tick = 0;
            glm::vec3 position{0.0f};
        };

        /**
         * @brief A body this end simulates because its character touches it, and for
         *        how many more ticks without being reported again.
         */
        struct Lease {
            EntityId entity;
            uint32_t ticks = 0;
        };

        /**
         * @brief What one body's picture owes, and is working off.
         *
         * Per body, since a client predicts what it leans on too; kept here, not
         * in the component, so no tick simulates from a cosmetic position.
         */
        struct DrawCorrection {
            EntityId  entity;
            glm::vec3 offset;  ///< Drawn minus simulated, still to work off.
        };

        /**
         * @brief One entity's prediction, kept across the reading of a snapshot
         *        that is about to describe an older moment for it.
         *
         * The snapshot goes in first so "unchanged" means the server's word;
         * what was held is then put back, or dropped for a replay to recompute.
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
         * One lifetime: dropped together when the session ends or changes world.
         */
        struct Prediction {
            std::vector<Predicted>      predicted;   ///< Where its own entity was, per tick.
            float                       error = 0.0f;
            std::vector<InputCommand>   unacknowledged;  ///< Not yet run by the server, oldest first.
            std::vector<InputCommand>   replay;      ///< The ticks a replay is re-running.
            bool                        replaying = false;

            uint32_t                    heard = 0;   ///< Newest command the server says has arrived.
            uint32_t                    sent  = 0;   ///< Newest command a packet has carried.

            /// How fast to make commands, from how low the server says their queue runs.
            NetPacing                   pacing;

            /**
             * @brief The bodies this end decides beyond its own: what it holds,
             *        what it took this snapshot, and what it has just given back.
             */
            std::vector<Lease>          leases;
            std::vector<EntityId>       acquired;
            std::vector<EntityId>       released;

            /// What the player last saw, and how far the picture still owes.
            std::vector<Held>           held;
            std::vector<DrawCorrection> corrections;

            bool warnedNoReplay = false;  ///< Said once each time the link outruns a replay.

            void clear() { *this = Prediction{}; }
        };

        /**
         * @brief Where a body this end has just taken over was last drawn, read
         *        before the snapshot overwrites it.
         */
        struct DrawnFrom {
            EntityId  entity;
            glm::vec3 drawn{0.0f};
        };

    private:
        bool onWelcome(Scene& scene, BitReader& reader);
        bool onRefuse(BitReader& reader);
        bool onGoodbye();
        void onSnapshot(Scene& scene, BitReader& reader);

        /**
         * @brief Set aside what this end predicts, before a snapshot overwrites it.
         *
         * The character and every leased body, except one leased this snapshot:
         * that one holds an interpolated pose, so where it was drawn goes to
         * @p takenFrom instead, to be eased across.
         *
         * @param scene     The world before the snapshot is read into it.
         * @param takenFrom Cleared, then filled with each body leased this snapshot.
         */
        void holdPredicted(Scene& scene, std::vector<DrawnFrom>& takenFrom);

        /**
         * @brief Build what the server spawned into the roots the snapshots named.
         *
         * Roots still carrying a NetSpawn, lowest slot first, up to
         * NET_SPAWN_BUILDS_PER_FRAME; the NetSpawn is dropped once acted on. A
         * root already holding an instance, or part of one, is not built over.
         *
         * @param scene     The world holding the roots.
         * @param resources Resolves the prefabs' asset names.
         */
        void buildSpawned(Scene& scene, ResourceManager& resources);

        /**
         * @brief Build @p spawn's prefab into @p root, keeping what the server said.
         *
         * A root with no Transform takes the spawn's pose; replicated components
         * it already holds are newer than the file and are put back over it.
         *
         * @param scene     The world holding the root.
         * @param resources Resolves the prefab's asset names.
         * @param root      The entity the server named; becomes the instance root.
         * @param spawn     The prefab path and pose the server sent.
         */
        void buildInto(Scene& scene, ResourceManager& resources, EntityId root, const NetSpawn& spawn);

        /**
         * @brief Judge this end's prediction against the server's answer for it.
         *
         * The prediction stands, or the ticks since @p confirmedTick are queued
         * to rerun from the snapshot already written into the scene.
         *
         * @param scene         The world with the snapshot already read into it.
         * @param confirmedTick Newest tick of this end's input the server has run.
         */
        void reconcile(Scene& scene, uint32_t confirmedTick);

        /**
         * @brief Put back what this end predicted, over what the snapshot wrote.
         *
         * All of it - character and pushed bodies are one answer to one
         * collision. An entity gone since is skipped, so this is safe after a
         * snapshot that decoded halfway.
         *
         * @param scene The world the snapshot was read into.
         */
        void restoreHeld(Scene& scene);
        void correctDraw(EntityId entity, const glm::vec3& drawn, const glm::vec3& simulated);
        void offsetDrawn(Scene& scene, EntityId entity, const glm::vec3& by);
        glm::vec3 drawOffsetFor(EntityId entity) const;

        /**
         * @brief Take the tick a snapshot claims, unless it cannot be believed.
         *
         * @param serverTick The tick the snapshot's header names.
         * @return False if the snapshot is to be refused whole.
         */
        bool adoptServerTick(uint32_t serverTick);

    private:
        NetCore& m_core;

        std::unique_ptr<NetConnection> m_connection;

        PlayerId m_localPlayer = NO_PLAYER;
        EntityId m_localEntity;
        uint64_t m_token = 0;  ///< Proves this end owns the seat.

        bool m_warnedTickJump = false;  ///< Said once, not once a packet.

        /// This end's own prediction, and everything it is kept in step with.
        Prediction m_prediction;

        NetInterpolation      m_interpolation;
        std::vector<uint32_t> m_touched;
};

} // namespace Vkm::Engine
