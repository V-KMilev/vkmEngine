#define VKM_LOG_CATEGORY "NET"

#include "net/replication/snapshot.h"

#include <algorithm>

#include "logger.h"

#include "ecs/scene.h"
#include "net/transport/connection.h"
#include "net/wire/protocol.h"
#include "net/replication/silence.h"
#include "net/replication/spawn.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief How far behind the newest snapshot an acknowledgement can still arrive.
 *
 * Past this the snapshot is given up on and what it said is simply resent, which is
 * cheaper than remembering it forever on a link that dropped it.
 */
constexpr uint16_t ACK_WINDOW = 64;

/**
 * @brief Slots reported gone in one snapshot.
 *
 * More than this is a scene teardown, and the rest go in the next snapshot - they keep
 * being gathered until confirmed, so nothing is lost by capping.
 */
constexpr uint32_t MAX_LOST_PER_SNAPSHOT = 64;

/**
 * @brief A gap this small is written in seven bits, which covers every entity in a busy
 * region of a world.
 *
 * Larger gaps pay a full slot index.
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
 * @brief One entity that differs from what the receiver confirmed, with everything
 * needed to write it and to decide whether it is written at all.
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
 * @brief Encode everything about one entity that differs from what was confirmed.
 *
 * @return False when nothing differs, so the entity is not described at all.
 */
bool buildCandidate(const Scene& scene, const NetSchema& schema, uint16_t sequence,
                    EntityId entity, bool owner, NetBaseline& baseline, Candidate& out) {
    const uint32_t typeCount = static_cast<uint32_t>(schema.size());

    out.entity = entity;
    out.slot   = entity.slot();
    out.owner  = owner;

    uint32_t oldest = 0;
    for (uint32_t type = 0; type < typeCount; ++type) {
        const NetType& row = schema.types()[type];
        if (row.policy == NetPolicy::OwnerOnly && !owner) continue;

        // Asked before anything is spent on the answer. Most entities carry
        // some of the replicated types and not others, so without this every
        // one they lack costs a buffer and an encode to discover it.
        if (row.has && !row.has(scene, entity)) continue;

        std::vector<uint8_t> encoded;
        BitWriter writer(encoded, 256);
        if (!row.encode(scene, entity, writer)) continue;
        writer.finish();
        if (writer.overflowed()) {
            LOG_ERROR("component '%s' encoded past its buffer; not replicated this snapshot",
                      row.name.c_str());
            continue;
        }

        const NetBaseline::Confirmed* confirmed = baseline.find(entity, type);

        // Unchanged means say nothing - except the receiver's own entity, which
        // it overwrote with its prediction. Telling it otherwise leaves
        // reconciliation nothing to compare, and it corrects nothing.
        if (!owner && confirmed && confirmed->bytes == encoded) continue;

        const uint32_t age = confirmed ? static_cast<uint16_t>(sequence - confirmed->sequence)
                                       : UINT16_MAX;
        oldest = std::max(oldest, age);

        out.mask |= 1u << type;
        out.bits += static_cast<uint32_t>(writer.bitCount());
        out.components.push_back({std::move(encoded),
                                  static_cast<uint32_t>(writer.bitCount()), type});
    }

    if (out.mask == 0) return false;
    out.age   = oldest;
    out.bits += typeCount;
    return true;
}

/**
 * @brief Take candidates in priority order for as long as the budget allows.
 *
 * Priority decides who is considered; the write decides who fits, because an
 * entry's id is a gap from the one before and a gap is not known until the
 * order is. Estimated here at the smallest an entry can cost, so the packet is
 * not left part empty. The owner is taken whatever is left: it is what
 * reconciliation compares against, so deferring it corrects nothing.
 *
 * @param usedBits What the header has already spent.
 */
void chooseWithinBudget(const std::vector<Candidate>& candidates, uint32_t budgetBits,
                        uint32_t usedBits, std::vector<const Candidate*>& chosen,
                        NetSnapshotStats& stats) {
    for (const Candidate& candidate : candidates) {
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
 * @param writtenBits What the header and the owner's reserved room have spent.
 *                    The owner is charged before this runs, against the widest
 *                    gap an id can cost, because it is the one entry that can
 *                    carry the body past a budget the datagram was sized from.
 */
void writeChosen(const std::vector<const Candidate*>& chosen, uint32_t typeCount,
                 uint32_t budgetBits, uint32_t writtenBits, BitWriter& writer,
                 NetBaseline::Pending& pending, NetSnapshotStats& stats) {
    uint32_t previous = 0;
    for (const Candidate* candidate : chosen) {
        // The real cost, now this entry's gap is known. Skipped rather than
        // stopped, so a smaller entry behind it still goes and the owner is
        // written wherever slot order puts it.
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
        for (const Candidate::Encoded& component : candidate->components) {
            writer.append(component.bytes.data(), component.bits);
            pending.changes.push_back({candidate->entity, candidate->slot,
                                       component.type, component.bytes});
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
    const auto found = std::find_if(m_pending.begin(), m_pending.end(),
                                    [sequence](const Pending& p) { return p.sequence == sequence; });
    if (found == m_pending.end()) return;

    for (Pending::Change& change : found->changes) {
        EntityRecord& record = m_entities[change.slot];

        // A slot whose occupant has changed since this snapshot was written
        // describes an entity that is gone. Nothing it confirmed is true of the
        // one there now.
        if (record.entity != change.entity) record = EntityRecord{change.entity, {}};
        if (record.components.size() <= change.type) record.components.resize(change.type + 1);

        Confirmed& confirmed = record.components[change.type];

        // A bounded distance rather than a plain newer-than: out of order only
        // reaches back ACK_WINDOW, and a component left idle past half the
        // sequence space inverts the comparison and is then resent forever.
        const uint16_t heldIsNewerBy = static_cast<uint16_t>(confirmed.sequence - found->sequence);
        if (confirmed.held && heldIsNewerBy != 0 && heldIsNewerBy <= ACK_WINDOW) continue;

        confirmed.bytes    = std::move(change.bytes);
        confirmed.sequence = found->sequence;
        confirmed.held     = true;
    }
    for (uint32_t slot : found->forgotten) m_entities.erase(slot);

    m_pending.erase(found);
}

void NetBaseline::expire(uint16_t sequence) {
    m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(),
                                   [sequence](const Pending& p) {
                                       return static_cast<uint16_t>(sequence - p.sequence) > ACK_WINDOW;
                                   }),
                    m_pending.end());
}

void NetBaseline::gatherLost(const Scene& scene, std::vector<uint32_t>& out) const {
    out.clear();
    for (const auto& [slot, record] : m_entities) {
        if (!scene.isAliveAtIndex(slot)) out.push_back(slot);
    }
    // Sorted so the gap encoding works and so two runs of the same world
    // produce the same bytes; a hash map's order is neither.
    std::sort(out.begin(), out.end());
    if (out.size() > MAX_LOST_PER_SNAPSHOT) out.resize(MAX_LOST_PER_SNAPSHOT);
}

NetSnapshotStats writeSnapshot(const Scene& scene,
                               const NetSchema& schema,
                               uint16_t sequence,
                               const NetBudget& budget,
                               NetBaseline& baseline,
                               std::vector<uint8_t>& out) {
    NetSnapshotStats stats;
    const uint32_t typeCount = static_cast<uint32_t>(schema.size());

    baseline.expire(sequence);

    std::vector<uint32_t> lost;
    baseline.gatherLost(scene, lost);
    stats.entitiesLost = static_cast<uint32_t>(lost.size());

    // Everything that differs from what this connection confirmed. Encoded
    // rather than compared field by field, because the comparison that matters
    // is on the quantised bytes - a settled world must go quiet.
    std::vector<Candidate> candidates;
    const uint32_t ownerSlot = budget.owner ? budget.owner.slot() : UINT32_MAX;

    scene.forEachEntity([&](EntityId entity) {
        ++stats.entitiesConsidered;

        // Three reasons an entity is not described, asked as one so the editor
        // cannot answer differently - see NetSilence.
        if (netSilence(scene, entity) != NetSilence::None) return;

        Candidate candidate;
        if (buildCandidate(scene, schema, sequence, entity, entity.slot() == ownerSlot,
                           baseline, candidate)) {
            candidates.push_back(std::move(candidate));
        }
    });

    // The owner first: it is what reconciliation compares against, so deferring
    // it corrects nothing. Everything else by how long it has waited, so a
    // deferred entity rises rather than losing every round to whatever moved.
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) {
                         if (a.owner != b.owner) return a.owner;
                         if (a.age != b.age)     return a.age > b.age;
                         return a.slot < b.slot;
                     });

    const uint32_t budgetBits = budget.bytes * 8;
    // The count field, the lost slots, and the bit that ends the entry list.
    const uint32_t headerBits = 8 + static_cast<uint32_t>(lost.size()) * LARGE_GAP_BITS + 1;

    std::vector<const Candidate*> chosen;
    chooseWithinBudget(candidates, budgetBits, headerBits, chosen, stats);

    // Written in slot order so the id is a small gap from the one before.
    std::sort(chosen.begin(), chosen.end(),
              [](const Candidate* a, const Candidate* b) { return a->slot < b->slot; });

    out.clear();
    BitWriter writer(out, budget.bytes + 64);

    writer.bits(static_cast<uint32_t>(lost.size()), 8);
    for (uint32_t slot : lost) writer.bits(slot, LARGE_GAP_BITS);

    uint32_t written = headerBits;
    for (const Candidate* candidate : chosen) {
        if (candidate->owner) {
            written += 1 + gapBits(SMALL_GAP) + candidate->bits;
            break;
        }
    }

    NetBaseline::Pending pending;
    pending.sequence  = sequence;
    pending.forgotten = lost;

    writeChosen(chosen, typeCount, budgetBits, written, writer, pending, stats);

    writer.boolean(false);
    writer.finish();

    if (writer.overflowed()) {
        LOG_ERROR("snapshot overflowed its own budget; sending nothing this tick");
        out.clear();
        return stats;
    }

    baseline.hold(std::move(pending));
    stats.bytesWritten = static_cast<uint32_t>(out.size());
    return stats;
}

bool readSnapshot(Scene& scene, const NetSchema& schema, BitReader& reader,
                  std::vector<uint32_t>* touched) {
    const uint32_t typeCount = static_cast<uint32_t>(schema.size());

    const uint32_t lostCount = reader.bits(8);
    for (uint32_t i = 0; i < lostCount; ++i) {
        const uint32_t slot = reader.bits(LARGE_GAP_BITS);
        if (reader.failed()) return false;
        // isAliveAtIndex, not entityAt's truth: entityAt answers for any slot
        // in range. The subtree goes too - an instance's children have no name
        // on the wire, so this is the only word a client gets that they went.
        if (scene.isAliveAtIndex(slot)) {
            HierarchyOperations::destroyHierarchy(scene, scene.entityAt(slot));
        }
    }

    uint32_t previous = 0;
    while (reader.boolean()) {
        if (reader.failed()) return false;

        const uint32_t slot = previous + readGap(reader);
        if (slot == 0 || slot > NET_MAX_SLOT) return false;
        previous = slot;

        const uint32_t mask = reader.bits(typeCount);
        if (reader.failed()) return false;

        // Created rather than refused, so a slot the server made since is built
        // here.
        const EntityId entity = scene.isAliveAtIndex(slot) ? scene.entityAt(slot)
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
