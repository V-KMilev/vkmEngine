#pragma once

#include "core/system.h"

namespace Vkm::Engine {

/**
 * @brief Points the scene's key light at wherever the Environment says the sun is.
 *
 * The procedural sky is authored on the Environment, in elevation and azimuth,
 * because it is scene-global and has to work whether or not anything else
 * exists. A sky and the shadow-casting key light disagreeing about where the sun
 * is looks broken and is hard to diagnose, so the Environment wins: this writes
 * its direction into the first directional light it finds.
 *
 * At night the same light aims at the moon instead, so night has real direction,
 * shadows and speculars rather than a painted disc over flat ambient.
 *
 * Consequences worth knowing:
 * - With `sky.procedural` on, the key light is the sky's: its rotation, colour
 *   and intensity are all overwritten every frame, from `sky.lightColor` /
 *   `sky.lightIntensity` by day and the `night.moonlight*` pair after dark.
 *   Author those, not the Light. Shadow settings stay the light's.
 * - A scene with no directional light is fine: the sky still renders from the
 *   angles, with nothing casting sunlight.
 *
 * Runs in the Simulation stage, so the rotation it writes is in place before
 * HierarchySystem resolves world transforms in the Transform stage.
 */
class SkySystem : public System {
    public:
        SkySystem() = default;
        ~SkySystem() override = default;

        SkySystem(const SkySystem& other) = delete;
        SkySystem& operator=(const SkySystem& other) = delete;

        SkySystem(SkySystem && other) = delete;
        SkySystem& operator=(SkySystem && other) = delete;

    public:
        void update(FrameContext& ctx) override;
};

} // namespace Vkm::Engine
