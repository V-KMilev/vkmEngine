#pragma once

#include "resource/asset/audio_clip_asset.h"

namespace Vkm::Engine {

/**
 * @brief Procedural sound for gameplay code, built without the tools module.
 *
 * The sibling of proc_mesh.h, and there for the same reason: an SDK installs
 * src/engine and nothing else, so a project cannot reach the importers - and
 * this project ships no audio file to import anyway, because its whole world is
 * generated. A clip is interleaved PCM and a rate, so a game can build one the
 * way it builds a cube.
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

} // namespace Vkm::Engine
