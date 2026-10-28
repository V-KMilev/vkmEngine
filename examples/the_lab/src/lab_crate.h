#pragma once

#include <string>

#include <glm/glm.hpp>

#include "system/script/reflected_behavior.h"

#include "lab_state.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief A thrown crate: it knocks down a character it hits, puffs where it first lands, and goes.
 *
 * The server decides: a character the crate strikes hard enough falls with the crate's momentum
 * (LabWalker::knockDown), the first strike is counted on the crate's LabTally, and the crate is
 * destroyed after `lifetime` so a session cannot fill the world. Every end makes the `impact`
 * prefab where the crate is when it sees that count move.
 */
class LabCrate : public ReflectedBehavior<LabCrate> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;
        void onFixedUpdate(float dt) override;
        void onCollisionEnter(const Collision& hit) override;

    public:
        float lifetime = 6.0f;  ///< Seconds it lasts once thrown.

        /// Prefab made where it first strikes something; empty makes none.
        PrefabRef impact{"prefabs/impact.json"};

    private:
        glm::vec3  m_momentum{0.0f};  ///< Before this tick's contacts, kg m/s; the authority's
        float      m_age = 0.0f;      ///< Seconds since it was thrown; the authority's
        CountWatch m_strikes;         ///< The strikes, to puff for a new one
};

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabCrate)
    VKM_F(lifetime)
    VKM_F(impact)
VKM_REFLECT_END()
