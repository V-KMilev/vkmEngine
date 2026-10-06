#pragma once

#include "system/script/behavior_api.h"

namespace Game {

// Your types stay in Game: adding to Vkm::Engine can collide with a later engine type.
using namespace Vkm::Engine;

/**
 * @brief Spins whatever entity it is attached to, and turns back when something hits it.
 *
 * ReflectedBehavior generates its name, fields and copy from the VKM_REFLECT
 * block below, so the editor and scene files see the fields; only hooks are hand-written.
 */
class Spinner : public ReflectedBehavior<Spinner> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;
        void onCollisionEnter(const Collision& hit) override;

    public:
        float     degreesPerSecond = 90.0f;
        glm::vec3 axis             = {0.0f, 1.0f, 0.0f};
};

} // namespace Game

// At global scope, the type named in full: the macro opens Vkm::Engine::Reflect itself.
VKM_REFLECT_BEGIN(::Game::Spinner)
    VKM_F(degreesPerSecond)
    VKM_F(axis)
VKM_REFLECT_END()
