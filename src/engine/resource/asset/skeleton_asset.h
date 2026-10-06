#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/component/core/transform.h"
#include "resource/resource.h"
#include "resource/resource_handle.h"

namespace Vkm::Engine {

/**
 * @brief One joint of a rig: its authoring name and the index of its parent.
 *
 * The name is the joint's only durable identity (see SkeletonAsset::indexOf).
 */
struct Bone {
    std::string name;
    int32_t     parent = -1;  ///< -1 for a root. INVARIANT: parent < this bone's own index.
};

/**
 * @brief A rig: a flat, parent-before-child bone array plus its bind pose.
 *
 * Bones are indices, not entities. `parent < index` is validated by
 * findSkeletonFault, so a pose composes in one forward loop and a cycle is
 * unrepresentable. The three vectors are parallel.
 */
struct SkeletonAsset : public Resource {
    std::vector<Bone>      bones;
    std::vector<glm::mat4> inverseBind;  ///< Rig model space -> this bone's space, at bind.

    /**
     * @brief Each bone's local TRS at bind, used wherever a clip has no channel
     *        for it.
     *
     * Stored: deriving it from `inverseBind` is lossy once a bone carries scale.
     */
    std::vector<Transform> bindPose;

    /**
     * @brief The index of the bone called @p name, or -1 when the rig has none.
     *
     * Linear; resolve once and keep the index rather than calling it per frame.
     *
     * @param name Bone name to look for.
     * @return Index into `bones`, or -1.
     */
    int32_t indexOf(std::string_view name) const;
};

/**
 * @brief Why @p skeleton cannot be posed, or an empty string when it can.
 *
 * What composing a pose relies on unchecked: parallel arrays, and every bone's
 * parent a bone before it.
 *
 * @param skeleton Rig to judge.
 * @return The first fault found, worded to follow "cannot be posed - ".
 */
std::string findSkeletonFault(const SkeletonAsset& skeleton);

using SkeletonHandle = Handle<SkeletonAsset>;

} // namespace Vkm::Engine
