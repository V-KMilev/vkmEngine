#pragma once

#include "system/script/reflected_behavior.h"

namespace Game {

/**
 * @brief Spins whatever entity it is attached to.
 *
 * The smallest complete behavior: one reflected field the editor can edit and a
 * scene file can persist, and one lifecycle hook. Deriving from
 * ReflectedBehavior generates typeName(), visitFields() and clone() from the
 * VKM_REFLECT block, so only the hook below is hand-written.
 */
class Spinner : public Vkm::Engine::ReflectedBehavior<Spinner> {
    public:
        static constexpr const char* TYPE_NAME = "Spinner";

        void onUpdate(float dt) override;

    public:
        float degreesPerSecond = 90.0f;
};

} // namespace Game

// At global scope, with the type named in full: the macro opens Vkm::Engine::Reflect
// itself, so your gameplay types stay in your own namespace.
VKM_REFLECT_BEGIN(Game::Spinner)
    VKM_F(degreesPerSecond)
VKM_REFLECT_END()
