#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ecs/entity.h"
#include "net/transport/connection.h"
#include "net/wire/schema.h"

namespace Vkm::Engine {

class NetSilenceMap;
class Scene;

/**
 * @brief What one connection is known to hold, so the next snapshot says less.
 *
 * A component is sent, whole, when its encoded bits differ from what this
 * connection has *confirmed receiving*: the delta is on presence, never on
 * values, so every entry decodes from the packet alone and a joiner's first
 * snapshot is the whole world by the same rule.
 *
 * Confirmed, not sent, or a lost packet would never be resent: what a snapshot
 * said is held until acknowledged. Per connection, since clients lose
 * different packets. Comparing encoded bits lets a settled world go quiet.
 */
class NetBaseline {
    public:
        /// One component of one entity, as this connection last confirmed it.
        struct Confirmed {
            std::vector<uint8_t> bytes;
            uint16_t             sequence = 0;  ///< Which snapshot established it.
            bool                 held     = false;
        };

        /// What a written snapshot claimed, held until it is acknowledged.
        struct Pending {
            struct Change {
                EntityId             entity;    ///< Which occupant of the slot it described.
                uint32_t             slot = 0;
                uint32_t             type = 0;
                std::vector<uint8_t> bytes;
            };

            uint16_t              sequence = 0;
            std::vector<Change>   changes;
            std::vector<EntityId> forgotten;  ///< Occupants reported as gone.
        };

    public:
        NetBaseline() = default;
        ~NetBaseline() = default;

        NetBaseline(const NetBaseline& other) = delete;
        NetBaseline& operator=(const NetBaseline& other) = delete;

        NetBaseline(NetBaseline && other) = delete;
        NetBaseline& operator=(NetBaseline && other) = delete;

    public:
        /**
         * @brief What is confirmed for @p entity's component @p type, or null.
         *
         * By entity, not slot: a slot can be reused before the old occupant's
         * record is freed, and the new one would be taken as already held.
         *
         * @param entity The occupant asked about.
         * @param type   The component's index in the schema.
         * @return The confirmed record, or null when none is held for that occupant.
         */
        const Confirmed* find(EntityId entity, uint32_t type) const;

        /// Hold what a snapshot said until it is confirmed or given up on.
        void hold(Pending && pending);

        /**
         * @brief Fold the snapshot @p sequence said into what is confirmed.
         *
         * A change is folded only when newer than what the slot holds, so a
         * late acknowledgement cannot write stale bytes over fresh.
         *
         * @param sequence The acknowledged snapshot; one not held is ignored.
         */
        void confirm(uint16_t sequence);

        /**
         * @brief Give up on everything older than @p sequence by more than the window
         *        an acknowledgement can still arrive in.
         *
         * What they said is never confirmed, so the next snapshot says it again.
         *
         * @param sequence The snapshot about to be written.
         */
        void expire(uint16_t sequence);

        /**
         * @brief Entities this connection was told about that are no longer alive.
         *
         * Reported until confirmed, which makes destruction reliable. By
         * entity, not slot: a slot reused between snapshots is alive both times.
         *
         * @param scene The world, to ask what is still in it.
         * @param out   Filled with the entities, ascending by slot.
         */
        void gatherLost(const Scene& scene, std::vector<EntityId>& out) const;

        /**
         * @brief Forget every entity and every unconfirmed snapshot.
         *
         * For a world replaced under a live session: a replacement reuses slot
         * *and* generation, defeating @ref find's guard.
         */
        void clear() {
            m_entities.clear();
            m_pending.clear();
        }

        size_t knownEntities() const { return m_entities.size(); }
        size_t pendingSnapshots() const { return m_pending.size(); }

    private:
        struct EntityRecord {
            EntityId               entity;  ///< Which occupant these describe.
            std::vector<Confirmed> components;
        };

    private:
        std::unordered_map<uint32_t, EntityRecord> m_entities;
        std::vector<Pending>                       m_pending;
};

/**
 * @brief How much of the world one snapshot may describe.
 *
 * A ceiling, or a busy frame makes a datagram the socket refuses whole. What
 * does not fit is deferred, its priority rising until it goes.
 */
struct NetBudget {
    /**
     * @brief Bytes one snapshot body may occupy.
     *
     * A session passes what its own header leaves.
     */
    uint32_t bytes = static_cast<uint32_t>(NetConnection::MAX_PAYLOAD);

    /**
     * @brief The connection's own entity, which is never the one deferred.
     *
     * Written first whatever the budget: reconciliation compares against it.
     */
    EntityId owner;
};

/**
 * @brief What one snapshot said, for the session's status line.
 *
 * A rising deferred count is a budget too small for the world; a rising lost
 * count is churn.
 */
struct NetSnapshotStats {
    uint32_t entitiesConsidered = 0;  ///< Entities in the world walked.
    uint32_t entitiesWritten    = 0;  ///< Of those, the ones that differed from the baseline.
    uint32_t componentsWritten  = 0;  ///< Component records inside them.
    uint32_t entitiesDeferred   = 0;  ///< Left out for want of budget; first in line next snapshot.
    uint32_t entitiesLost       = 0;  ///< Announced as gone: the receiver holds them, the scene does not.
    uint32_t bytesWritten       = 0;  ///< The body's size, headers excluded.
};

/**
 * @brief Write the part of @p scene that @p baseline does not already hold.
 *
 * What does not fit is deferred. What this claimed is held pending in
 * @p baseline until acknowledged; nothing is held when @p out overflows, so an
 * unsent packet leaves no claim for its sequence. An entity @p silence keeps
 * off the wire is still described by its NetSpawn, and nothing else.
 *
 * @param scene    The authoritative world.
 * @param schema   What replicates, agreed with the receiver.
 * @param silence  What stays off the wire, classified once for the round.
 * @param sequence The snapshot's own sequence, so its claim can be confirmed.
 * @param budget   The body's ceiling, and the receiver's own entity.
 * @param baseline Gains a pending record of what this snapshot said.
 * @param out      The packet, appended to.
 * @return What was written, for the session's status line.
 */
NetSnapshotStats writeSnapshot(
    const Scene& scene,
    const NetSchema& schema,
    const NetSilenceMap& silence,
    uint16_t sequence,
    const NetBudget& budget,
    NetBaseline& baseline,
    BitWriter& out
);

/**
 * @brief Apply a snapshot body to @p scene.
 *
 * Entities are addressed by scene slot, which both ends agree on from the same
 * scene file; there is no mapping table.
 *
 * @param scene   The world the body is applied to.
 * @param schema  What replicates, agreed with the sender.
 * @param in      The packet, at the start of the body.
 * @param touched Filled, when given, with the slots this snapshot said
 *                anything about.
 * @return False when the body is malformed. What was applied before the fault
 *         stays: nothing refused is confirmed, so the next snapshot says it again.
 */
bool readSnapshot(
    Scene& scene,
    const NetSchema& schema,
    BitReader& in,
    std::vector<uint32_t>* touched = nullptr
);

} // namespace Vkm::Engine
