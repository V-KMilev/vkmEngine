#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ecs/entity.h"
#include "net/wire/schema.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief What one connection is known to hold, so the next snapshot says less.
 *
 * A snapshot carries a component only when its encoded bits differ from what
 * this connection has *confirmed receiving*. The delta is on presence, never on
 * values, which is what makes it safe: a value delta chains, so one lost packet
 * poisons everything after it, while an absolute entry held back for three
 * snapshots is correct the moment it lands. It is also why joining and playing
 * are one code path - a connection that has confirmed nothing differs from the
 * world in every component, so its first snapshot is the whole world by the
 * rule rather than by a special case.
 *
 * Confirmed, not sent: folding a snapshot in when it is written describes a
 * client that received it, and a client that did not is never told again. So
 * what a snapshot said is held aside until its acknowledgement comes back, and
 * dropped unfolded when it does not. Per connection, never shared, because two
 * clients lose different packets.
 *
 * Comparison is on encoded bits, not floats: two positions a micrometre apart
 * encode identically and a settled world goes quiet.
 */
class NetBaseline {
    public:
        NetBaseline() = default;
        ~NetBaseline() = default;

        NetBaseline(const NetBaseline& other) = delete;
        NetBaseline& operator=(const NetBaseline& other) = delete;

        NetBaseline(NetBaseline && other) = delete;
        NetBaseline& operator=(NetBaseline && other) = delete;

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
            std::vector<uint32_t> forgotten;  ///< Slots reported as gone.
        };

    public:
        /**
         * @brief What is confirmed for @p entity's component @p type, or null.
         *
         * The entity and not just its slot: a slot outlives its occupants, and a
         * record still naming the last one would let presence-delta decide the
         * receiver already holds a value for an entity it has never seen. The
         * lost report frees a record eventually, but only once it is confirmed,
         * and a slot can be reused before that.
         */
        const Confirmed* find(EntityId entity, uint32_t type) const;

        /// Hold what a snapshot said until it is confirmed or given up on.
        void hold(Pending && pending);

        /**
         * @brief Fold the snapshot @p sequence said into what is confirmed.
         *
         * Out of order is normal, so a change is folded only when it is newer
         * than what the slot already holds. Without that guard, an
         * acknowledgement for an older snapshot arriving after a newer one
         * would write stale bytes over fresh, and the server would then believe
         * a client holds a position it has already replaced.
         */
        void confirm(uint16_t sequence);

        /**
         * @brief Give up on everything older than @p sequence by more than the window
         * an acknowledgement can still arrive in.
         *
         * What they said is simply never confirmed, so the next snapshot says it again.
         */
        void expire(uint16_t sequence);

        /**
         * @brief Slots this connection was told about that are no longer alive.
         *
         * Reported until the report is confirmed, which is what makes
         * destruction reliable without a channel of its own.
         *
         * @param scene The world, to ask what is still in it.
         * @param out   Filled with the slots, ascending.
         */
        void gatherLost(const Scene& scene, std::vector<uint32_t>& out) const;

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
 * A ceiling rather than a target. Letting a busy frame produce whatever it
 * produces is what turns a collapsing tower into a datagram the socket refuses
 * whole - so the client sees nothing at exactly the moment there is most to
 * see. What does not fit is not dropped, it is deferred, and it goes first next
 * time because its priority has been rising while it waited.
 */
struct NetBudget {
    /// Bytes one snapshot body may occupy.
    uint32_t bytes = 1100;

    /**
     * @brief The connection's own entity, which is never the one deferred.
     *
     * Written before anything is prioritised and kept whatever the budget: it is
     * the anchor reconciliation compares against, and a snapshot without it
     * corrects nothing.
     */
    EntityId owner;
};

/// What a snapshot said, for the log line and the editor's panel.
struct NetSnapshotStats {
    uint32_t entitiesConsidered = 0;
    uint32_t entitiesWritten    = 0;
    uint32_t componentsWritten  = 0;
    uint32_t entitiesDeferred   = 0;
    uint32_t entitiesLost       = 0;
    uint32_t bytesWritten       = 0;
};

/**
 * @brief Write the part of @p scene that @p baseline does not already hold.
 *
 * What does not fit the budget is deferred rather than dropped, and goes first
 * next time. The caller owes the result to the wire and the baseline to an
 * acknowledgement: what this claimed is held pending until the receiver
 * confirms it, because a claim folded in at send time describes a client that
 * may never have got it.
 *
 * @param scene    The authoritative world.
 * @param schema   What replicates, agreed with the receiver.
 * @param sequence The snapshot's own sequence, so its claim can be confirmed.
 * @param budget   The ceiling, and which entity is the receiver's own.
 * @param baseline Gains a pending record of what this snapshot said.
 * @param out      Filled with the snapshot body.
 * @return What was written, for the log line and the editor's panel.
 */
NetSnapshotStats writeSnapshot(const Scene& scene,
                               const NetSchema& schema,
                               uint16_t sequence,
                               const NetBudget& budget,
                               NetBaseline& baseline,
                               std::vector<uint8_t>& out);

/**
 * @brief Apply a snapshot body to @p scene.
 *
 * Entities are addressed by scene slot, which both ends already agree on
 * because the scene file records it and both loaded the same file. There is no
 * mapping table and no handshake for identity.
 *
 * Reads from wherever @p in stands, rather than from the start of a buffer, so
 * a caller never computes where the body begins - which is a byte offset that
 * has to be recomputed every time the header changes and is silently wrong when
 * it is not.
 *
 * @param touched Filled, when given, with the slots this snapshot said
 *                anything about - so a caller that keeps its own record of what
 *                the authority last said knows which records to update without
 *                walking the world.
 * @return False when the body is malformed. What was applied before the fault
 *         stays applied, which is safe: the receiver confirms nothing it
 *         refused, so the next snapshot describes all of it again.
 */
bool readSnapshot(Scene& scene, const NetSchema& schema, BitReader& in,
                  std::vector<uint32_t>* touched = nullptr);

} // namespace Vkm::Engine
