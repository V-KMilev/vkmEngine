#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "resource/asset_ref.h"
#include "resource/asset/audio_clip_asset.h"
#include "system/script/behavior.h"

#include "lab_state.h"

namespace Vkm::Engine {
    class Scene;
}

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief A token hidden somewhere in the lab: a player who walks into it has found it.
 *
 * Its collider is a trigger. The server sets the token's bit, `index`, in the finder's LabScore,
 * so each player finds each token once. Each end hides the tokens its own player has found, and
 * when one is found bursts its child emitter named `Burst` and chimes. The child named `Gem`
 * turns and bobs.
 */
class LabToken : public Behavior<LabToken> {
    public:
        void onStart() override;

        /// Its bit in LabScore::tokens; none for an index out of range.
        uint32_t bit() const;
        void onUpdate(float dt) override;
        void onTriggerEnter(EntityId other) override;

    public:
        int   index   = 0;
        float spin    = 90.0f;
        float bob     = 0.12f;
        int   sparkle = 60;

        AssetRef<AudioClipAsset> chime;

    private:
        /**
         * @brief Show or hide everything under the token but the burst.
         *
         * @param shown Whether this end's player has yet to find it.
         */
        void show(bool shown);

    private:
        std::vector<EntityId> m_parts;           ///< The token and everything under it
        EntityId              m_gem{};
        EntityId              m_burst{};
        glm::vec3             m_gemRest{0.0f};   ///< The gem's authored place
        CountWatch            m_finds;           ///< Whether this end's player has found it, 0 or 1
        bool                  m_hidden = false;  ///< Whether show() last hid it
};

} // namespace Lab

VKM_BEHAVIOR(::Lab::LabToken)
    VKM_F(index, "Its bit in LabScore::tokens, 0 to 31; unique in the scene.")
    VKM_F(spin, "Degrees a second the gem turns.")
    VKM_F(bob, "Meters the gem rises and falls.")
    VKM_F(sparkle, "Particles the burst makes when it is found.")
    VKM_F(chime, "Heard where it is found.")
VKM_REFLECT_END()
