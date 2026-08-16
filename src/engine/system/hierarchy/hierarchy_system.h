#pragma once

#include <array>
#include <vector>

#include "core/system.h"
#include "system/hierarchy/hierarchy_operations.h"

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

    private:
        using DepthBuckets = std::array<std::vector<EntityId>, HierarchyOperations::MAX_DEPTH>;

        /**
         * @brief Resolve world transforms for every dirty hierarchical entity.
         *
         * For each entity with a Hierarchy whose dirty flag is set, computes its
         * world matrix and writes it into the entity's pre-seeded WorldTransform.
         * Dirty entities are bucketed by absolute depth in a serial pass and then
         * each bucket runs through parallelFor; depths are processed in order so a
         * child reads its parent's already-finalised WorldTransform (one matrix
         * multiply, parentWorld * local) rather than re-walking the ancestor chain,
         * and reads of an ancestor's matrix never race a write.
         *
         * @param scene The scene to resolve.
         */
        void resolve(Scene& scene);

    private:
        DepthBuckets m_buckets;  ///< Per-depth scratch for the resolve pass; kept for its capacity.
};

} // namespace Engine
