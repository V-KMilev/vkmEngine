#pragma once

#include "system/script/reflected_behavior.h"

namespace Engine {

/**
 * @brief Example behavior: drives its entity around the XZ plane with WASD.
 *
 * Demonstrates input from a behavior. World-axis movement (W = -Z, S = +Z,
 * A = -X, D = +X); the editor fly-cam only consumes WASD while the right mouse
 * button is held, so in play mode plain WASD moves the player, not the camera.
 */
class PlayerController : public ReflectedBehavior<PlayerController> {
    public:
        static constexpr const char* TYPE_NAME = "PlayerController";

        void onUpdate(float dt) override;

        /** @brief Authored, reflected: movement speed in world units per second. */
        float speed = 3.0f;
};

} // namespace Engine

VKM_REFLECT_BEGIN(::Engine::PlayerController)
    VKM_F(speed)
VKM_REFLECT_END()
