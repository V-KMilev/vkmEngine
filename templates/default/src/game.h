#pragma once

#include "system/script/behavior_api.h"

namespace Game {

// Your types stay in Game: adding to Vkm::Engine can collide with a later engine type.
using namespace Vkm::Engine;

/**
 * @brief Spins whatever entity it is attached to, and turns back when something hits it.
 *
 * Behavior<Spinner> generates its name, fields and copy from the VKM_BEHAVIOR
 * block below, so the editor and scene files see the fields; only hooks are hand-written.
 */
class Spinner : public Behavior<Spinner> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;
        void onCollisionEnter(const Collision& hit) override;

    public:
        float     degreesPerSecond = 90.0f;
        glm::vec3 spinAxis         = {0.0f, 1.0f, 0.0f};
};

} // namespace Game

// At global scope, the type named in full: the macro opens Vkm::Engine::Reflect itself.
VKM_BEHAVIOR(::Game::Spinner)
    VKM_F(degreesPerSecond, -720.0f, 720.0f, "How fast it turns; negative turns the other way")
    VKM_F(spinAxis, "What it turns about")
VKM_REFLECT_END()
