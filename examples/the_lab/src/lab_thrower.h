#pragma once

#include <string>

#include "system/script/behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief On its player's key, throws a crate the way the character faces.
 *
 * The server throws: the crate is a prefab built through NetSession::spawn, so every end - later
 * joiners too - builds it, and what it does next is the crate's own (LabCrate). Offline only the
 * seat throws.
 */
class LabThrower : public Behavior<LabThrower> {
    public:
        void onFixedUpdate(float dt) override;

    public:
        PrefabRef crate{"prefabs/crate.json"};

        float speed    = 14.0f;
        float cooldown = 0.4f;

    private:
        float m_wait = 0.0f;  ///< Seconds until the next throw; the authority's
};

} // namespace Lab

VKM_BEHAVIOR(::Lab::LabThrower)
    VKM_F(crate, "The prefab thrown.")
    VKM_F(speed, "Launch speed along the character's facing, m/s.")
    VKM_F(cooldown, "Seconds between two throws.")
VKM_REFLECT_END()
