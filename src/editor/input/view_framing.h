#pragma once

#include <glm/glm.hpp>

#include "ecs/entity.h"

namespace Vkm::Engine {

struct FrameContext;
class CameraControllerSystem;

/**
 * @brief Frame Selected and Frame All.
 *
 * They move only the view, so take no undo step. A stood-down controller refuses
 * the move.
 */
namespace ViewFraming {

/**
 * @brief Frame @p selected: centre on its world-space mesh bounds, or its origin.
 *
 * No-op when it is dead or has no Transform.
 *
 * @param ctx Supplies the scene and mesh bounds.
 * @param selected Entity to frame.
 * @param camera View moved.
 */
void frameSelected(const FrameContext& ctx, EntityId selected, CameraControllerSystem& camera);

/**
 * @brief Frame the union of this frame's visible boxes.
 *
 * No-op when nothing is visible.
 *
 * @param ctx Its visibility result supplies the boxes.
 * @param camera View moved.
 */
void frameAll(const FrameContext& ctx, CameraControllerSystem& camera);

/**
 * @brief How far from @p target a viewpoint must stand to be outside everything drawn.
 *
 * Every render object counts, culled or not, since an orthographic view culls what is
 * behind its near plane.
 *
 * @param ctx    Supplies the frame's render objects.
 * @param target The point the view will look at.
 * @return World units: the furthest bounds corner from @p target, or 0 with nothing drawn.
 */
float clearance(const FrameContext& ctx, const glm::vec3& target);

} // namespace ViewFraming

} // namespace Vkm::Engine
