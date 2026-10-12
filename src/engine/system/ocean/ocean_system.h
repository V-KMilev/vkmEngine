#pragma once

#include "core/system.h"
#include "system/ocean/ocean_data.h"

namespace Vkm::Engine {

/**
 * @brief Builds the scene's sea (findOcean's pick) once a tick and once a frame, and publishes it
 *        as FrameContext::ocean, for gameplay, the cull and the renderer alike.
 *
 * At the moment NetSession::worldTime gives: inside a tick, on the authority, that tick's own, so
 * what a tick floats on it floats the same way every time; in the frame, the moment drawn. At
 * Simulation, registered ahead of BehaviorSystem, so a behavior's ocean() is this tick's or this
 * frame's sea. Not replayed: a replayed tick sees the sea the last one built.
 */
class OceanSystem : public System {
    public:
        OceanSystem() = default;
        ~OceanSystem() override = default;

        OceanSystem(const OceanSystem& other) = delete;
        OceanSystem& operator=(const OceanSystem& other) = delete;

        OceanSystem(OceanSystem && other) = delete;
        OceanSystem& operator=(OceanSystem && other) = delete;

    public:
        void update(FrameContext& ctx) override;
        void fixedUpdate(FrameContext& ctx) override;

    private:
        /// Rebuild m_ocean from the scene at worldTime, and publish it.
        void build(FrameContext& ctx);

    private:
        OceanData m_ocean;
};

} // namespace Vkm::Engine
