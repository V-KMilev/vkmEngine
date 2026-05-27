#pragma once

#include <memory>

namespace Core {
    class Texture2D;
}

namespace Engine {

/**
 * @brief Build the pre-integrated subsurface LUT (Penner GPU Gems 3 ch. 14).
 *
 * Numerically integrates max(cos(theta), 0) along the surface against a
 * per-channel Gaussian profile (red widest, blue narrowest) for every
 * (NdotL, curvature) cell. Returns a 64x16 RGBA32F texture indexed in the
 * PBR shader's HAS_SUBSURFACE branch as:
 *   X (64) - NdotL remapped from [-1, 1] to [0, 1]
 *   Y (16) - curvature in (0, 1], driven at the call site by
 *            length(fwidth(N)) * material.subsurface
 *
 * Ran once at startup by GLForwardPass; the ~65-tap-per-texel integration
 * cost is irrelevant at this resolution. Pure function, no GL state side
 * effects beyond the texture allocation.
 */
std::unique_ptr<Core::Texture2D> makeSSSLUT();

} // namespace Engine
