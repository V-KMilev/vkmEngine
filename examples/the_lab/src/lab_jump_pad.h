#pragma once

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "resource/asset_ref.h"
#include "resource/asset/audio_clip_asset.h"
#include "system/script/reflected_behavior.h"

#include "lab_state.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief A pad that throws whoever lands on it into the air: a trampoline, a launcher, a cannon.
 *
 * A character standing on it (Rigidbody::supportBody) is launched by its own LabWalker, on the
 * tick, so a client predicts the throw as the server makes it. The server counts each launch on
 * this entity's LabTally, and every end shows it: the child emitters burst, the child named `Bed`
 * dips, and `boing` sounds. The count reaches clients only if the entity is on the wire - authored
 * with a Kinematic body, not a Static one - and onStart reports one that is not.
 */
class LabJumpPad : public ReflectedBehavior<LabJumpPad> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;

        /// Count a launch; the authority's alone, the rest are told.
        void launched();

    public:
        float launchSpeed = 13.0f;  ///< Upward speed it gives, m/s.
        float forward     = 0.0f;   ///< Speed it adds along its own forward, m/s.
        int   puff        = 24;     ///< Particles each child emitter bursts at a launch.

        AssetRef<AudioClipAsset> boing;  ///< Heard at each launch.

    private:
        EntityId   m_bed{};
        glm::vec3  m_bedScale{1.0f};  ///< The bed's authored scale, at rest
        CountWatch m_launches;        ///< The launches, to show each new one
        float      m_dip = 0.0f;      ///< How far into its dip the bed is, 1 at the bottom
};

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabJumpPad)
    VKM_F(launchSpeed)
    VKM_F(forward)
    VKM_F(puff)
    VKM_F(boing)
VKM_REFLECT_END()
