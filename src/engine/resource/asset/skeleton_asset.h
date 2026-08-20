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
 * The name is the joint's only durable identity - a clip binds to bone indices
 * by matching it at cook time, and it is what an attachment or a physics body
 * names later. `parent` indexes the same array this bone lives in.
 */
struct Bone {
    std::string name;
    int32_t     parent = -1;  ///< -1 for a root. INVARIANT: parent < this bone's own index.
};

/**
 * @brief A rig: a flat, parent-before-child bone array plus its bind pose.
 *
 * Bones are indices rather than entities. A hundred entities per character
 * would be walked by the hierarchy, listed in the panel and written to the
 * scene file, for data that is rebuilt every frame and has no authoring
 * meaning; an index also maps straight onto a body when physics comes to
 * address one.
 *
 * `parent < index` is a validated format invariant, not a convention: the
 * importer emits bones depth-first and the cooked reader re-checks the
 * ordering. That is what makes composing a pose one forward loop with no
 * recursion and no visited set, and what makes a cycle unrepresentable instead
 * of something every walk has to defend against.
 *
 * The three vectors are parallel and always the same length.
 */
struct SkeletonAsset : public Resource {
    std::vector<Bone>      bones;
    std::vector<glm::mat4> inverseBind;  ///< Rig model space -> this bone's space, at bind.

    /**
     * @brief Each bone's local TRS at bind, used wherever a clip has no channel
     *        for it.
     *
     * Stored rather than derived from `inverseBind`, because recovering it
     * means inverting and re-localising, which is lossy the moment a bone
     * carries scale.
     */
    std::vector<Transform> bindPose;

    /**
     * @brief The index of the bone called @p name, or -1 when the rig has none.
     *
     * Linear by design: a rig is a hundred bones, and this is called when a
     * clip is bound or an attachment resolved, never per frame.
     *
     * @param name Bone name to look for.
     * @return Index into `bones`, or -1 if no bone carries that name.
     */
    int32_t indexOf(std::string_view name) const;
};

using SkeletonHandle = Handle<SkeletonAsset>;

} // namespace Vkm::Engine
