#define VKM_LOG_CATEGORY "NET"

#include "net/replication/snapshot.h"

#include <algorithm>

#include "logger.h"

#include "debug/profiler.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "net/replication/silence.h"
#include "net/wire/codecs.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief How far behind the newest snapshot an acknowledgement can still arrive.
 *
 * Past this the snapshot is given up on and what it said is resent.
 */
constexpr uint16_t ACK_WINDOW = 64;

/**
 * @brief Slots reported gone in one snapshot.
 *
 * The rest go in the next snapshot; they are gathered until confirmed.
 */
constexpr uint32_t MAX_LOST_PER_SNAPSHOT = 64;

/**
 * @brief A gap this small is written in seven bits; larger ones pay a full slot index.
 */
constexpr uint32_t SMALL_GAP = 128;
constexpr uint32_t LARGE_GAP_BITS = 24;

void writeGap(BitWriter& out, uint32_t gap) {
    const bool small = gap < SMALL_GAP;
    out.boolean(small);
    out.bits(gap, small ? 7 : LARGE_GAP_BITS);
}

uint32_t readGap(BitReader& in) {
    const bool small = in.boolean();
    return in.bits(small ? 7 : LARGE_GAP_BITS);
}

/// Bits a gap will cost, so the budget is decided on the real figure.
uint32_t gapBits(uint32_t gap) {
    return 1 + (gap < SMALL_GAP ? 7 : LARGE_GAP_BITS);
}

/**
 * @brief One entity that differs from what the receiver confirmed, ready to write.
 */
struct Candidate {
    /// One encoded component, parallel to a set bit of the mask below.
    struct Encoded {
        std::vector<uint8_t> bytes;
        uint32_t             bits = 0;   ///< Not bytes * 8; the tail is padding.
        uint32_t             type = 0;
    };

    EntityId             entity;       ///< Which occupant of the slot this is.
    uint32_t             slot  = 0;
    uint32_t             mask  = 0;
    std::vector<Encoded> components;
    uint32_t             bits  = 0;
    uint32_t             age   = 0;    ///< Snapshots since last confirmed.
    bool                 owner = false;
};

/**
 * @brief The most one component may occupy on the wire, per entity per snapshot.
 *
 * A bound on the *packet*: one big component would starve every other entity.
 * Past it the component is dropped for that snapshot, and logged. A spawn
 * sets it: the longest path plus NET_SPAWN_MAX_SLOTS slots.
 */
constexpr uint32_t MAX_COMPONENT_BYTES = 512;

static_assert(
    NET_SPAWN_MAX_BYTES <= MAX_COMPONENT_BYTES,
    "a spawn naming the longest path has to fit the room one component may take"
);

/**
 * @brief Encode everything about one entity that differs from what was confirmed.
 *
 * Encoded per connection though the bytes are the same for every peer; see
 * engine.md, "Encoding a snapshot once per peer".
 *
 * @param scene     The authoritative world.
 * @param schema    What replicates.
 * @param sequence  The snapshot being written, to age what was confirmed against.
 * @param entity    The entity to encode.
 * @param owner     Whether it is the receiver's own, which is resent unchanged.
 * @param silenced  Whether its state stays off the wire; only its NetSpawn is
 *                  then considered.
 * @param spawnType NetSpawn's index in @p schema, or -1 when it has none.
 * @param baseline  What this receiver has confirmed.
 * @param scratch   Reused encode buffer, so an unchanged component costs no
 *                  allocation.
 * @param out       Filled with what differs.
 * @return False when nothing differs, so the entity is not described at all.
 */
bool buildCandidate(
    const Scene& scene,
    const NetSchema& schema,
    uint16_t sequence,
    EntityId entity,
    bool owner,
    bool silenced,
    int spawnType,
    NetBaseline& baseline,
    std::vector<uint8_t>& scratch,
    Candidate& out
) {
    const uint32_t typeCount = static_cast<uint32_t>(schema.size());

    out.entity = entity;
    out.slot   = entity.slot();
    out.owner  = owner;

    uint32_t oldest = 0;
    for (uint32_t type = 0; type < typeCount; ++type) {
        const NetType& row     = schema.types()[type];
        const bool     isSpawn = static_cast<int>(type) == spawnType;
        if (silenced && !isSpawn) continue;

        // Asked first, so a type the entity lacks costs no encode.
        if (!row.has(scene, entity)) continue;

        BitWriter writer(scratch, MAX_COMPONENT_BYTES);
        row.encode(scene, entity, writer);
        writer.finish();
        if (writer.overflowed()) {
            LOG_ERROR(
                "component '%s' encoded past its %u-byte limit; not replicated this snapshot",
                row.name.c_str(),
                MAX_COMPONENT_BYTES
            );
            continue;
        }

        const NetBaseline::Confirmed* confirmed = baseline.find(entity, type);

        // Unchanged says nothing, except to the receiver about its own entity,
        // which it overwrote with its prediction and reconciles against. Its
        // NetSpawn was never predicted, so it is said once like anyone's.
        const bool resent = owner && !isSpawn;
        if (!resent && confirmed && confirmed->bytes == scratch) continue;

        const uint32_t age = confirmed ? static_cast<uint16_t>(sequence - confirmed->sequence) : UINT16_MAX;
        oldest = std::max(oldest, age);

        out.mask |= 1u << type;
        out.bits += static_cast<uint32_t>(writer.bitCount());
        out.components.push_back({scratch, static_cast<uint32_t>(writer.bitCount()), type});
    }

    if (out.mask == 0) return false;
    out.age   = oldest;
    out.bits += typeCount;
    return true;
}

/**
 * @brief What one call of writeSnapshot works in, kept between calls.
 *
 * Cleared, not rebuilt, so the lists keep their capacity across peers and
 * rounds; what outlives the call is copied into the pending record.
 */
struct SnapshotScratch {
    std::vector<EntityId>   lost;
    std::vector<uint32_t>   unreported;
    std::vector<Candidate>  candidates;
    std::vector<Candidate*> chosen;
    std::vector<uint8_t>    encoded;
};

thread_local SnapshotScratch t_scratch;

/**
 * @brief Take candidates in priority order for as long as the budget allows.
 *
 * The write decides who really fits, since an id's gap is unknown until the
 * order is; here each costs its smallest, so the packet is not left part
 * empty. The owner is taken whatever is left.
 *
 * @param candidates Every entity that differs, in priority order.
 * @param budgetBits The ceiling for the body.
 * @param usedBits   What the header has already spent.
 * @param chosen     Receives the candidates taken, still in priority order.
 * @param stats      Counts each one deferred.
 */
void chooseWithinBudget(
    std::vector<Candidate>& candidates,
    uint32_t budgetBits,
    uint32_t usedBits,
    std::vector<Candidate*>& chosen,
    NetSnapshotStats& stats
) {
    for (Candidate& candidate : candidates) {
        const uint32_t cost = candidate.bits + gapBits(1) + 1;
        if (usedBits + cost > budgetBits && !candidate.owner) {
            ++stats.entitiesDeferred;
            continue;
        }
        usedBits += cost;
        chosen.push_back(&candidate);
    }
}

/**
 * @brief Write the chosen entries, in the slot order they were sorted into.
 *
 * @param chosen      The entries, by slot; each one's bytes move into @p pending.
 * @param typeCount   Width of each entry's component mask.
 * @param budgetBits  The ceiling for the body.
 * @param writtenBits What the header and the owner's reserved room have spent;
 *                    the owner is charged at the widest gap, as the one entry
 *                    that may exceed the budget.
 * @param writer      The packet.
 * @param pending     Gains one change per component written.
 * @param stats       Counts what was written and what was deferred.
 */
void writeChosen(
    const std::vector<Candidate*>& chosen,
    uint32_t typeCount,
    uint32_t budgetBits,
    uint32_t writtenBits,
    BitWriter& writer,
    NetBaseline::Pending& pending,
    NetSnapshotStats& stats
) {
    uint32_t previous = 0;
    for (Candidate* candidate : chosen) {
        // Skipped, not stopped, so a smaller entry behind still goes and the
        // owner is written wherever slot order puts it.
        const uint32_t gap  = candidate->slot - previous;
        const uint32_t cost = 1 + gapBits(gap) + candidate->bits;
        if (!candidate->owner) {
            if (writtenBits + cost > budgetBits) {
                ++stats.entitiesDeferred;
                continue;
            }
            writtenBits += cost;
        }

        writer.boolean(true);
        writeGap(writer, gap);
        previous = candidate->slot;

        writer.bits(candidate->mask, typeCount);
        // Moved: the candidates die with this snapshot.
        for (Candidate::Encoded& component : candidate->components) {
            writer.append(component.bytes.data(), component.bits);
            pending.changes.push_back(
                {candidate->entity, candidate->slot, component.type, std::move(component.bytes)}
            );
        }
        ++stats.entitiesWritten;
        stats.componentsWritten += static_cast<uint32_t>(candidate->components.size());
    }
}

} // namespace

const NetBaseline::Confirmed* NetBaseline::find(EntityId entity, uint32_t type) const {
    const auto found = m_entities.find(entity.slot());
    if (found == m_entities.end() || found->second.entity != entity) return nullptr;
    if (type >= found->second.components.size()) return nullptr;
    const Confirmed& confirmed = found->second.components[type];
    return confirmed.held ? &confirmed : nullptr;
}

void NetBaseline::hold(Pending && pending) {
    m_pending.push_back(std::move(pending));
}

void NetBaseline::confirm(uint16_t sequence) {
    const auto found = std::find_if(
        m_pending.begin(),
        m_pending.end(),
        [sequence](const Pending& p) { return p.sequence == sequence; }
    );
    if (found == m_pending.end()) return;

    for (Pending::Change& change : found->changes) {
        EntityRecord& record = m_entities[change.slot];

        // The occupant changed since: nothing confirmed is true of the new one.
        if (record.entity != change.entity) record = EntityRecord{change.entity, {}};
        if (record.components.size() <= change.type) record.components.resize(change.type + 1);

        Confirmed& confirmed = record.components[change.type];

        // A bounded distance, not plain newer-than: a component idle past half
        // the sequence space would invert it and be resent forever.
        const uint16_t heldIsNewerBy = static_cast<uint16_t>(confirmed.sequence - found->sequence);
        if (confirmed.held && heldIsNewerBy != 0 && heldIsNewerBy <= ACK_WINDOW) continue;

        confirmed.bytes    = std::move(change.bytes);
        confirmed.sequence = found->sequence;
        confirmed.held     = true;
    }
    // Only the reported occupant: whatever took the slot since may be confirmed.
    for (EntityId gone : found->forgotten) {
        const auto record = m_entities.find(gone.slot());
        if (record != m_entities.end() && record->second.entity == gone) m_entities.erase(record);
    }

    m_pending.erase(found);
}

void NetBaseline::expire(uint16_t sequence) {
    const auto expired = std::remove_if(
        m_pending.begin(),
        m_pending.end(),
        [sequence](const Pending& p) { return static_cast<uint16_t>(sequence - p.sequence) > ACK_WINDOW; }
    );
    m_pending.erase(expired, m_pending.end());
}

void NetBaseline::gatherLost(const Scene& scene, std::vector<EntityId>& out) const {
    out.clear();
    for (const auto& [slot, record] : m_entities) {
        if (!scene.isAlive(record.entity)) out.push_back(record.entity);
    }
    // Sorted, for the gap encoding and for the same bytes from the same world.
    std::sort(out.begin(), out.end(), [](EntityId a, EntityId b) { return a.slot() < b.slot(); });
}

NetSnapshotStats writeSnapshot(
    const Scene& scene,
    const NetSchema& schema,
    const NetSilenceMap& silence,
    uint16_t sequence,
    const NetBudget& budget,
    NetBaseline& baseline,
    BitWriter& out
) {
    PROFILE_SCOPE("writeSnapshot");
    NetSnapshotStats stats;
    const uint32_t typeCount = static_cast<uint32_t>(schema.size());
    const size_t   startBits = out.bitCount();

    baseline.expire(sequence);

    SnapshotScratch&         scratch    = t_scratch;
    std::vector<EntityId>&   lost       = scratch.lost;
    std::vector<uint32_t>&   unreported = scratch.unreported;
    std::vector<Candidate>&  candidates = scratch.candidates;
    std::vector<Candidate*>& chosen     = scratch.chosen;
    unreported.clear();
    candidates.clear();
    chosen.clear();

    baseline.gatherLost(scene, lost);

    // Past the cap, a slot's new occupant is not described until the old is
    // reported gone, or it would decode onto the old one.
    for (size_t i = MAX_LOST_PER_SNAPSHOT; i < lost.size(); ++i) unreported.push_back(lost[i].slot());
    if (lost.size() > MAX_LOST_PER_SNAPSHOT) lost.resize(MAX_LOST_PER_SNAPSHOT);
    stats.entitiesLost = static_cast<uint32_t>(lost.size());

    const uint32_t ownerSlot = budget.owner ? budget.owner.slot() : UINT32_MAX;
    const int      spawnType = schema.indexOf(NET_SPAWN_TYPE);

    scene.forEachEntity([&](EntityId entity) {
        ++stats.entitiesConsidered;

        // Silence keeps state off the wire, never what an entity is: its
        // NetSpawn is still said.
        const bool silenced = silence.of(entity) != NetSilence::None;
        if (silenced && (spawnType < 0 || !schema.types()[spawnType].has(scene, entity))) return;

        if (std::binary_search(unreported.begin(), unreported.end(), entity.slot())) {
            ++stats.entitiesDeferred;
            return;
        }

        Candidate candidate;
        if (buildCandidate(
            scene,
            schema,
            sequence,
            entity,
            entity.slot() == ownerSlot,
            silenced,
            spawnType,
            baseline,
            scratch.encoded,
            candidate
        )) {
            candidates.push_back(std::move(candidate));
        }
    });

    // The owner first, then by how long each has waited, so deferral rises.
    std::stable_sort(
        candidates.begin(),
        candidates.end(),
        [](const Candidate& a, const Candidate& b) {
            if (a.owner != b.owner) return a.owner;
            if (a.age != b.age)     return a.age > b.age;
            return a.slot < b.slot;
        }
    );

    const uint32_t budgetBits = budget.bytes * 8;
    // The count field, the lost slots, and the bit that ends the entry list.
    const uint32_t headerBits = 8 + static_cast<uint32_t>(lost.size()) * LARGE_GAP_BITS + 1;

    chooseWithinBudget(candidates, budgetBits, headerBits, chosen, stats);

    // Written in slot order so the id is a small gap from the one before.
    std::sort(
        chosen.begin(),
        chosen.end(),
        [](const Candidate* a, const Candidate* b) { return a->slot < b->slot; }
    );

    out.bits(static_cast<uint32_t>(lost.size()), 8);
    for (EntityId gone : lost) out.bits(gone.slot(), LARGE_GAP_BITS);

    uint32_t written = headerBits;
    for (const Candidate* candidate : chosen) {
        if (candidate->owner) {
            written += 1 + gapBits(SMALL_GAP) + candidate->bits;
            break;
        }
    }

    NetBaseline::Pending pending;
    pending.sequence  = sequence;
    pending.forgotten.assign(lost.begin(), lost.end());

    writeChosen(chosen, typeCount, budgetBits, written, out, pending, stats);
    out.boolean(false);

    if (out.overflowed()) return stats;

    baseline.hold(std::move(pending));
    stats.bytesWritten = static_cast<uint32_t>((out.bitCount() - startBits + 7u) / 8u);
    return stats;
}

bool readSnapshot(Scene& scene, const NetSchema& schema, BitReader& reader, std::vector<uint32_t>* touched) {
    PROFILE_SCOPE("readSnapshot");
    const uint32_t typeCount = static_cast<uint32_t>(schema.size());

    const uint32_t lostCount = reader.bits(8);
    for (uint32_t i = 0; i < lostCount; ++i) {
        const uint32_t slot = reader.bits(LARGE_GAP_BITS);
        if (reader.failed()) return false;
        // entityAt answers for any slot in range. The subtree goes too: an
        // instance's children have no name on the wire.
        if (scene.isAliveAtIndex(slot)) {
            HierarchyOperations::destroyHierarchy(scene, scene.entityAt(slot));
        }
    }

    uint32_t previous = 0;
    while (reader.boolean()) {
        if (reader.failed()) return false;

        const uint32_t slot = previous + readGap(reader);
        previous = slot;

        const uint32_t mask = reader.bits(typeCount);
        if (reader.failed()) return false;

        // Created, not refused: the server may have made it since.
        const EntityId entity = scene.isAliveAtIndex(slot)
            ? scene.entityAt(slot)
            : scene.createEntityAt(slot);
        if (!entity) return false;

        for (uint32_t type = 0; type < typeCount; ++type) {
            if (!(mask & (1u << type))) continue;
            if (!schema.types()[type].decode(scene, entity, reader)) return false;
        }
        if (touched) touched->push_back(slot);
    }
    return !reader.failed();
}

} // namespace Vkm::Engine
