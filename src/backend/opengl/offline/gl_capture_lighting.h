#pragma once

#include <cstdint>

namespace Vkm::Engine {

/**
 * @brief What lit a capture: the lights, the sky and the irradiance volume's light.
 *
 * A capture made under other light is out of date. The volume's own surfels leave the volume
 * fields zero: no other volume lights them.
 */
struct CaptureLighting {
    uint64_t lights          = 0;      ///< lightingDigest of the frame's lights.
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

} // namespace Vkm::Engine
