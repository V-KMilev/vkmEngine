#pragma once

#include "core/system.h"

namespace Engine {

/**
 * @brief Resolves hierarchical transforms into a WorldTransform component.
 *
 * Runs before VisibilitySystem. For every entity with a Hierarchy component,
 * computes the world-space model matrix by walking the parent chain and writes
 * it to a WorldTransform component (added on demand). Downstream systems read
 * WorldTransform when present, falling back to Transform for root entities.
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
};

} // namespace Engine
