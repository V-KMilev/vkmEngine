#pragma once

#include "resource/asset/audio_clip_asset.h"

namespace Vkm::Engine {

/**
 * @brief Procedural sound for gameplay code, built without the tools module.
 *
 * Here because the engine has no audio equivalent of its mesh generators: a
 * project cannot reach the importers, and this one ships no audio file to
 * import anyway, because its whole world is generated. A clip is interleaved
 * PCM and a rate, so a game can build one the way it builds a cube - which it
 * now does through the engine's own generateCube, this file's mesh sibling
 * having moved into `resource/generate` where both examples could share it.
 */

/**
 * @brief Synthesize a boot landing on concrete: a short mono thud.
 *
 * A pitch-dropping body under a burst of damped noise, which is what a footfall
 * is - a low impact and the scuff over it. Both ends are ramped, because a
 * waveform that starts or stops at a non-zero sample clicks, and a click is what
 * this would otherwise be heard as at the fifty-a-minute a run fires it.
 *
 * Deterministic: the noise comes from a seeded engine Rng, so every run of the
 * game gets the identical clip rather than one that differs per launch.
 *
 * @return A ~0.13 second mono clip at 44100 Hz, unnamed - the caller names it
 *         when it registers it, like every other asset this project builds.
 */
AudioClipAsset makeFootstepSound();

/**
 * @brief Synthesize a coin pickup: a short two-partial chime.
 *
 * A fifth above the fundamental, both decaying exponentially, which is the
 * cheapest thing that reads as metal rather than as a beep - one partial alone
 * is a test tone. Ramped at both ends for the same reason the footstep is.
 * Longer than the footfall (0.18 s against 0.13) because a struck chime rings,
 * but deliberately no louder: it peaks at 0.68 to the footstep's 0.68 and sits
 * lower in rms, because a run collects coins while the feet keep landing and
 * the chime has to cut through the stride without covering it.
 *
 * Deterministic: the small amount of noise in the strike comes from a seeded
 * engine Rng, so the clip is identical on every launch. What varies between
 * pickups is the playback (gain and pitch), not the sound.
 *
 * @return A ~0.18 second mono clip at 44100 Hz, unnamed - the caller names it
 *         when it registers it, like every other asset this project builds.
 */
AudioClipAsset makeCoinChime();

} // namespace Vkm::Engine
