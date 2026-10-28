#pragma once

#include <cstdint>
#include <vector>

#include "core/math/bounds.h"

namespace Vkm::Engine {

struct LightData;

/**
 * @brief What lit a capture: the lights, the sky and the irradiance volume's light.
 *
 * A capture made under other light is out of date. The volume's own surfels leave the volume
 * fields zero: no other volume lights them.
 */
struct CaptureLighting {
    uint64_t lights          = 0;      ///< lightingDigest of the lights that reach the capture.
    uint32_t sky             = 0;      ///< GLIBL::version.
    bool     skyReady        = false;
    uint32_t volumeLight     = 0;      ///< GLIrradianceVolume::lightId of the volume lighting it; 0 for none.
    float    volumeIntensity = 0.0f;   ///< That volume's intensity.

    bool operator==(const CaptureLighting& other) const {
        return lights == other.lights && sky == other.sky && skyReady == other.skyReady
            && volumeLight == other.volumeLight && volumeIntensity == other.volumeIntensity;
    }

    bool operator!=(const CaptureLighting& other) const { return !(*this == other); }
};

/**
 * @brief A digest of what a capture reads of @p lights, field by field (no padding).
 *
 * Per type, what the irradiance relight and a probe capture read: a directional's slot and shadow
 * too, since the lowest-slot caster is the shadowed sun; no other light casts in a capture, and a
 * directional has no position, so a sun parented to a mover lights nothing anew. shadowDistance and
 * shadowPriority are left out, as a capture fits and keeps its own map; a flicker's wave, as a
 * capture takes a wavering light steady; and a light not in captures (LightData::inCaptures)
 * whole, as a capture gives it no light.
 *
 * @param lights The frame's lights.
 * @param region When given, only the directionals and the lights whose radius reaches it: a lamp
 *               moving elsewhere does not make a capture here out of date.
 * @return Their digest; equal digests light a capture alike.
 */
uint64_t lightingDigest(const std::vector<LightData>& lights, const Math::AABB* region = nullptr);

} // namespace Vkm::Engine
