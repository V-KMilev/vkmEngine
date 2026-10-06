#pragma once

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

} // namespace ViewFraming

} // namespace Vkm::Engine
