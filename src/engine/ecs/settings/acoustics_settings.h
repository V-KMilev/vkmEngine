#pragma once

#include "base/reflect/reflect.h"

namespace Vkm::Engine {

/**
 * @brief Sound shaped by the world's colliders: muffled behind what stands between a source and
 *        the ear, and ringing in the space the ear is in (docs/reference/audio.md, "Acoustics").
 */
struct AcousticsSettings {
    bool  enabled = true;
    float reverb  = 1.0f;  ///< Scale on the room's reverberation; 0 keeps the occlusion alone.
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::AcousticsSettings)
    VKM_F(enabled, "Sound follows the colliders: muffled behind them, ringing in rooms")
    VKM_F(reverb, 0.0f, 2.0f, "Scales the room's reverberation; 0 keeps the muffling alone")
VKM_REFLECT_END()
