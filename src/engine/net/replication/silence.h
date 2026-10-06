#pragma once

#include <cstdint>
#include <vector>

#include "ecs/entity.h"
#include "system/physics/ragdoll_system.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Is @p entity a body that stays where the scene file put it?
 *
 * Such a body never moves, so both ends hold the scene file's exact value;
 * describing it would replace that with a quantised copy, whose angle error
 * grows with size - ten centimetres across a sixty-metre floor.
 *
 * @param scene  World holding the entity.
 * @param entity Entity to test.
 * @return True when it carries a Rigidbody whose motion is RigidbodyMotion::Static.
 */
bool isStaticBody(const Scene& scene, EntityId entity);

/**
 * @brief Why an entity is not described on the wire, or None when it is.
 *
 * Prefab::isInsideInstance, isPosedByAnimation and isStaticBody, asked in one
 * place so the snapshot and what an author is told cannot disagree.
 */
enum class NetSilence : uint8_t {
    None,                  ///< It is on the wire.

    /**
     * @brief Its root speaks for it.
     *
     * Both ends build the subtree from the same prefab into the same slots, so
     * only the root is described. A ragdoll therefore belongs on a character
     * the scene placed: inside a spawned instance its bones would fall on the
     * server alone.
     */
    InsidePrefabInstance,
    PosedByAnimation,      ///< Both ends work it out; see isPosedByAnimation.
    StaticBody,            ///< It cannot move, and the scene file is exact.
    Count
};

/**
 * @brief Which of the rules keeps @p entity off the wire, if any.
 *
 * @param scene  World holding the entity.
 * @param entity Entity to classify.
 * @return Why it is off the wire, or None.
 */
NetSilence netSilence(const Scene& scene, EntityId entity);

/**
 * @brief netSilence for every entity of a world, classified in one pass.
 *
 * Built once per send round: the answer depends on the world, not the peer,
 * and is costly per entity.
 */
class NetSilenceMap {
    public:
        NetSilenceMap() = default;
        ~NetSilenceMap() = default;

        NetSilenceMap(const NetSilenceMap& other) = delete;
        NetSilenceMap& operator=(const NetSilenceMap& other) = delete;

        NetSilenceMap(NetSilenceMap && other) = delete;
        NetSilenceMap& operator=(NetSilenceMap && other) = delete;

    public:
        /**
         * @brief Classify every entity in @p scene, replacing what was there.
         *
         * @param scene The world as this round describes it.
         */
        void build(const Scene& scene);

        /**
         * @brief What netSilence says of @p entity, as of the last build.
         *
         * @param entity An entity alive in the world the map was built from.
         * @return Why it is off the wire, or None.
         */
        NetSilence of(EntityId entity) const;

    private:
        std::vector<bool>       m_posed;   ///< Scratch for the build; see markPosedByAnimation.
        std::vector<NetSilence> m_bySlot;  ///< None for any slot past the end.
};

/**
 * @brief What to tell an author about @p reason.
 *
 * @param reason Why the entity is not described.
 * @return The explanation, or nullptr when the entity replicates.
 */
const char* toString(NetSilence reason);

} // namespace Vkm::Engine
