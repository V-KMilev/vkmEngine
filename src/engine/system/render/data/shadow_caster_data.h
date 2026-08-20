#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

/**
 * @brief One shadow-casting object: geometry, world placement, and world bounds.
 *
 * Collected scene-wide (not camera-culled) so off-screen occluders still cast
 * into the visible scene. The world AABB lets the shadow pass frustum-cull
 * casters per light / cascade before drawing their depth.
 */
struct ShadowCasterData {
    MeshHandle mesh;
    glm::mat4  model;
    glm::vec3  aabbMin;
    glm::vec3  aabbMax;
};

/**
 * @brief Where one caster's bones sit in RenderView::skinMatrices.
 *
 * Casters carry a palette of their own, and not only the drawables, because the
 * two lists are gathered from different sets: a character standing just
 * off-screen and casting into view appears in this one alone, and posing it in
 * the camera list only would leave its shadow frozen in bind pose.
 *
 * It rides alongside ShadowCasterData rather than inside it, for the same reason
 * the rig binding is a second vertex buffer instead of four more fields on
 * Vertex: the cull above reads every caster's bounds once per atlas tile and
 * once per cube face, and a scene with no characters must not widen that walk by
 * two fields the cull never looks at. Nothing is lost by moving them out,
 * because a caster is addressed by index everywhere - the cull's output is
 * indices into RenderView::shadowCasters - so a parallel array is as reachable
 * as a member would be.
 *
 * RenderView::casterSkins is empty on a frame that posed nothing, and otherwise
 * holds one of these per caster, index for index. A count of 0 means nothing
 * poses that caster and the shadow pass draws it with the static program, which
 * is also the honest answer for a skinned mesh dragged out of its rig's subtree:
 * the vertices it stored are its bind pose.
 */
struct ShadowCasterSkin {
    uint32_t first = 0;
    uint32_t count = 0;
};

} // namespace Vkm::Engine
