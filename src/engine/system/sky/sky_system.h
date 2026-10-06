#pragma once

#include "core/system.h"

namespace Vkm::Engine {

/**
 * @brief Points the scene's key light (findKeyLight) at the Environment's sun, or moon at night.
 *
 * With `sky.procedural` on, the key light's rotation, colour and intensity are
 * overwritten every frame from `sky.lightColor` / `sky.lightIntensity`
 * (through `Atmosphere::sunlight`) by day and `night.moonlight*` after dark;
 * author those, not the Light. Shadow settings stay the light's.
 *
 * Runs before HierarchySystem, so world transforms see the rotation it writes.
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
