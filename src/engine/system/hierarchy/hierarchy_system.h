#pragma once

#include <vector>

#include "core/system.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief Resolves hierarchical transforms into the WorldTransform component.
 *
 * Runs after Simulation and before VisibilitySystem. WorldTransform is pre-seeded by
 * HierarchyOperations::setParent, so this never mutates the component graph; an entity
 * without one is read from its Transform (see resolvedWorldMatrix). Every matrix is
 * resolved every frame; no dirty flag (docs/guides/engine.md, section 4).
 */
class HierarchySystem : public System {
    public:
        HierarchySystem() = default;
        ~HierarchySystem() override = default;

        HierarchySystem(const HierarchySystem& other) = delete;
        HierarchySystem& operator=(const HierarchySystem& other) = delete;

        HierarchySystem(HierarchySystem && other) = delete;
        HierarchySystem& operator=(HierarchySystem && other) = delete;

    public:
        void update(FrameContext& ctx) override;

    private:
        /**
         * @brief Resolve world transforms for every hierarchical entity.
         *
         * A level at a time from the roots, each through parallelFor, so a child reads its
         * parent's finalised matrix without a race. Each entity is visited from its own
         * parent, so a cycle, which no root reaches, is never visited.
         *
         * @param scene Scene whose WorldTransforms are written.
         */
        void resolve(Scene& scene);

    private:
        std::vector<EntityId> m_level;  ///< Kept for its capacity.
        std::vector<EntityId> m_next;   ///< The level below, gathered while resolving.
};

} // namespace Vkm::Engine
