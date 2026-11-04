#pragma once

#include <cstdint>
#include <vector>

#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Is @p entity a body that stays where the scene file put it?
 *
 * Such a body never moves, so both ends hold the scene file's exact value;
 * describing it would replace that with a quantized copy, whose angle error
 * grows with size - ten centimeters across a sixty-meter floor.
 *
 * @param scene  World holding the entity.
 * @param entity Entity to test.
 * @return True when it carries a Rigidbody whose motion is RigidbodyMotion::Static.
 */
bool isStaticBody(const Scene& scene, EntityId entity);

/**
 * @brief Is @p entity a mesh that says it never moves, with no body to move it?
 *
 * As a static body, it stays where the scene file put it; a quantized copy would move it, and the
 * cull, which keeps what it worked out of a fixed mesh, would take that for a broken promise.
 *
 * @param scene  World holding the entity.
 * @param entity Entity to test.
 * @return True when its Mesh has Mesh::movable off and it carries no Rigidbody.
 */
bool isFixedMesh(const Scene& scene, EntityId entity);

/**
 * @brief Why an entity is not described on the wire, or None when it is.
 *
 * NetLocal, Prefab::isInsideInstance, isPosedByAnimation, isStaticBody and isFixedMesh, asked in
 * one place so the snapshot and what an author is told cannot disagree.
 */
enum class NetSilence : uint8_t {
    None,                  ///< It is on the wire.

    /**
     * @brief Its root speaks for it.
     *
     * Both ends build the subtree from the same prefab, so only the root is
     * described. A ragdoll therefore belongs on a character
     * the scene placed: inside a spawned instance its bones would fall on the
     * server alone.
     */
    InsidePrefabInstance,
    PosedByAnimation,      ///< Both ends work it out; see isPosedByAnimation.
    StaticBody,            ///< It cannot move, and the scene file is exact.
    FixedMesh,             ///< Its mesh says it never moves, and the scene file is exact.
    Local,                 ///< It or an ancestor is NetLocal: each end runs its own.
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
 * @brief What an author is told about one reason an entity is off the wire.
 */
struct NetSilenceText {
    const char* label       = nullptr;  ///< A few words, to follow "Off the wire: ".
    const char* explanation = nullptr;  ///< Why that keeps it off, in full.
};

/**
 * @brief What to tell an author about @p reason.
 *
 * @param reason Why the entity is not described.
 * @return The label and explanation; both nullptr when the entity replicates.
 */
NetSilenceText explain(NetSilence reason);

} // namespace Vkm::Engine
