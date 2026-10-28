#pragma once

#include <memory>

#include "core/system.h"
#include "system/ocean/ocean_data.h"
#include "system/ocean/ocean_spectrum.h"

namespace Vkm::Engine {

/**
 * @brief Builds the scene's sea (findOcean's pick) once a tick and once a frame, and publishes it
 *        as FrameContext::ocean, for gameplay, the cull and the renderer alike.
 *
 * At the moment NetSession::worldTime gives: inside a tick, on the authority, that tick's own, so
 * what a tick floats on it floats the same way every time; in the frame, the moment drawn. At
 * Simulation, registered ahead of BehaviorSystem, so a behavior's ocean() is this tick's or this
 * frame's sea. Replayed, so a replayed tick has a sea to float on. The swell's spectrum
 * (OceanSwell) is made again only when what shapes it changes.
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

        /**
         * @brief Built from the scene at NetSession::worldTime, which on a client is the drawn
         *        moment's: a replayed tick floats on the sea drawn now, not the one it first floated on.
         */
        bool isReplayed() const override { return true; }

    private:
        /// Rebuild m_ocean from the scene at worldTime, and publish it.
        void build(FrameContext& ctx);

    private:
        OceanData                         m_ocean;
        std::shared_ptr<const OceanSwell> m_swell;   ///< The scene's ocean's, as m_shaped shapes it.
        OceanShape                        m_shaped;  ///< What m_swell was made from.
        bool                              m_made = false;
};

} // namespace Vkm::Engine
