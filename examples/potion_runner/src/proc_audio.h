#pragma once

#include "resource/asset/audio_clip_asset.h"

namespace Potion {

using namespace Vkm::Engine;

// Procedural sound: the engine has no audio counterpart to its mesh generators,
// and this generated world ships no audio file to import.

/**
 * @brief Synthesize a boot landing on concrete: a short mono thud.
 *
 * Deterministic (seeded Rng). Both ends are ramped: a non-zero first or last sample clicks.
 *
 * @return A ~0.13 second mono clip at 44100 Hz, unnamed.
 */
AudioClipAsset makeFootstepSound();

/**
 * @brief Synthesize a coin pickup: a short two-partial chime.
 *
 * Deterministic (seeded Rng), ramped like the footstep, and no louder than it
 * so the chime cuts through the stride without covering it.
 *
 * @return A ~0.18 second mono clip at 44100 Hz, unnamed.
 */
AudioClipAsset makeCoinChime();

} // namespace Potion
