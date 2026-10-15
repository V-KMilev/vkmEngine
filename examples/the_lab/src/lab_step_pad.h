#pragma once

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "resource/asset_ref.h"
#include "resource/asset/audio_clip_asset.h"
#include "system/script/reflected_behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief A tile that lights up and sounds when a character steps onto it: a key of a floor piano,
 *        a square of a dance floor.
 *
 * Every end judges it alike from where the characters stand, so nothing of it is on the wire. The
 * tile is `halfSize` either side of its entity across the ground, in its own frame. Its child named
 * `Glow` shows while a character stands on it, and for `glowSeconds` after.
 */
class LabStepPad : public ReflectedBehavior<LabStepPad> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;

    public:
        glm::vec2 halfSize{0.5f, 0.5f};  ///< Metres either side of the middle, along its X and Z.
        float     glowSeconds = 0.5f;    ///< How long it stays lit after it is left.

        AssetRef<AudioClipAsset> note;  ///< Heard as a character steps on.

    private:
        /// Whether a character stands on the tile now.
        bool isStoodOn();

    private:
        EntityId m_glow{};
        bool     m_stood = false;  ///< Stood on, as of the last frame
        float    m_lit   = 0.0f;   ///< Seconds it stays lit
};

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabStepPad)
    VKM_F(halfSize)
    VKM_F(glowSeconds)
    VKM_F(note)
VKM_REFLECT_END()
