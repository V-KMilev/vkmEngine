#pragma once

#include <string>

#include "system/script/reflected_behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief On its player's key, throws a crate the way the character faces.
 *
 * The server throws: the crate is a prefab built through NetSession::spawn, so every end - later
 * joiners too - builds it, and what it does next is the crate's own (LabCrate). Offline only the
 * seat throws.
 */
class LabThrower : public ReflectedBehavior<LabThrower> {
    public:
        void onStart() override;
        void onFixedUpdate(float dt) override;

    public:
        std::string crate = "prefabs/crate.json";  ///< The prefab thrown, project-relative.

        float speed    = 14.0f;  ///< Launch speed along the character's facing, m/s.
        float cooldown = 0.4f;   ///< Seconds between two throws.

    private:
        float m_wait = 0.0f;  ///< Seconds until the next throw; the authority's
};

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabThrower)
    VKM_F(crate)
    VKM_F(speed)
    VKM_F(cooldown)
VKM_REFLECT_END()
