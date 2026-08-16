#pragma once

#include "core/system.h"

namespace Engine {

/**
 * @brief Points the scene's key light at wherever the Environment says the sun is.
 *
 * The procedural sky is authored on the Environment, in elevation and azimuth,
 * because the sky is scene-global and has to work whether or not anything else
 * exists. But a sky and the light casting the scene's shadows disagreeing about
 * where the sun is looks broken in a way that is hard to diagnose, so one of
 * them has to follow the other. The Environment wins: it is where the sun is
 * authored, and this writes that direction into the first directional light it
 * finds.
 *
 * At night the same light aims at the moon instead, so night has real direction,
 * shadows and speculars rather than a painted disc over flat ambient. The two
 * bodies are opposite each other, so this is a swap, not a blend - and it
 * happens while the light is contributing nothing, because each body's own
 * contribution fades out as it reaches the horizon.
 *
 * Consequences worth knowing:
 * - With `sky.procedural` on, the key light is the sky's: its rotation, colour
 *   and intensity are all overwritten every frame, from `sky.lightColor` /
 *   `sky.lightIntensity` by day and the `night.moonlight*` pair after dark.
 *   Author those, not the Light. Shadow settings stay the light's.
 * - A scene with no directional light is fine. The sky still renders from the
 *   angles; there is simply nothing casting sunlight, which is visible rather
 *   than mysterious.
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

} // namespace Engine
