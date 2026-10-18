#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "resource/asset_ref.h"
#include "resource/asset/audio_clip_asset.h"
#include "system/script/reflected_behavior.h"

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
class LabToken : public ReflectedBehavior<LabToken> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;
        void onTriggerEnter(EntityId other) override;

    public:
        int   index   = 0;      ///< Its bit in LabScore::tokens, 0 to 31; unique in the scene.
        float spin    = 90.0f;  ///< Degrees a second the gem turns.
        float bob     = 0.12f;  ///< Metres the gem rises and falls.
        int   sparkle = 60;     ///< Particles the burst makes when it is found.

        AssetRef<AudioClipAsset> chime;  ///< Heard where it is found.

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

/**
 * @brief The bits of every LabToken in @p scene.
 *
 * @param scene Scene to search.
 * @return A mask with each token's `index` set.
 */
uint32_t everyToken(Scene& scene);

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabToken)
    VKM_F(index)
    VKM_F(spin)
    VKM_F(bob)
    VKM_F(sparkle)
    VKM_F(chime)
VKM_REFLECT_END()
