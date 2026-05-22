#pragma once

#include "core/system.h"

namespace Engine {

/**
 * @brief Resolves hierarchical transforms into the WorldTransform component.
 *
 * Runs in the Transform stage (before VisibilitySystem). For each dirty
 * entity with a Hierarchy component, computes its world-space model matrix
 * by walking the parent chain and writes the result into its WorldTransform
 * (pre-seeded by HierarchyOperations::setParent so this loop never has to
 * mutate the component graph - which is what lets it parallelise over
 * depth buckets). Downstream systems read WorldTransform when present and
 * fall back to Transform for root entities.
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

        /**
         * @brief Reads the local Transform and the Hierarchy parent link; writes
         *
         * only the resolved WorldTransform (pre-seeded at setParent time,
         * so this loop never adds storage). Safe to share a parallel
         * layer with anything else that reads Transform/Hierarchy and
         * doesn't touch WorldTransform.
         */
        SystemAccess declareAccess() const override;
};

} // namespace Engine
