#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "core/system.h"
#include "ecs/settings/wave_settings.h"
#include "ecs/water/water_data.h"

namespace Vkm::Engine {

struct MeshAsset;

/**
 * @brief Builds the scene's water and its waves, and floats what is in it.
 *
 * Every entity with a Water and a Mesh is a body of water (WaterBody): its surface, as its entity
 * draws it, laid on a grid of heights about its origin - again whenever its mesh, turn or scale
 * changes, and only then, as a move carries the grid; its Water's values are read as they stand.
 * The waves are the environment's (Environment::waves) at the moment NetSession::worldTime gives:
 * inside a tick, on the authority, that tick's own, so what a tick floats it floats the same way
 * every time; in the frame, the moment drawn. Their swell (Swell) is made again only when what
 * shapes it changes, and only while a water takes a share of it. Built into the scene's water
 * (Scene::water) once a tick and once a frame, for gameplay, the cull and the renderer alike; each
 * tick it then floats every Buoyancy body on the water it is in, and starts drawing the next tick's
 * swell on a worker. A scene with no Water is none of this work. At Simulation, ahead of BehaviorSystem and
 * physics.
 */
class WaterSystem : public System {
    public:
        static constexpr float    LEAST_CELL = 0.25f;  ///< Meters: the narrowest a grid's cells are, else as
                                                       ///< wide as its mesh's vertices are apart.
        static constexpr uint32_t MOST_CELLS = 512;    ///< The most cells across a body: a larger body's
                                                       ///< cells widen.

    public:
        WaterSystem() = default;
        ~WaterSystem() override = default;

        WaterSystem(const WaterSystem& other) = delete;
        WaterSystem& operator=(const WaterSystem& other) = delete;

        WaterSystem(WaterSystem && other) = delete;
        WaterSystem& operator=(WaterSystem && other) = delete;

    public:
        void update(FrameContext& ctx) override;
        void fixedUpdate(FrameContext& ctx) override;

        /**
         * @brief A float is a function of the world: a replayed tick builds the water again at
         *        NetSession::worldTime, which on a client is the drawn moment's, and lifts it on it.
         */
        bool isReplayed() const override { return true; }

        /**
         * @brief Lay @p mesh's surface, drawn by @p model, on @p body's grid.
         *
         * @param body  Filled: its grid and cells.
         * @param mesh  The surface's geometry.
         * @param model Its world matrix.
         */
        static void layOut(WaterBody& body, const MeshAsset& mesh, const glm::mat4& model);

    private:
        /// Lay out any body changed, build the waves when any water is, and publish them.
        void build(FrameContext& ctx);

        /// The waves at worldTime: the swell made again only if its shape changed and a body takes it.
        void buildWaves(FrameContext& ctx, bool swelling);

        /**
         * @brief Float every Buoyancy body this end simulates on the water it is in.
         *
         * @return Whether it asked the water about any: whether a tick floats anything.
         */
        bool floatBodies(FrameContext& ctx, float dt);

        /// Draw the next tick's swell on a worker, so the tick that floats on it finds it drawn.
        void drawAhead(const FrameContext& ctx);

    private:
        std::vector<WaterBody>             m_kept;    ///< build()'s next bodies, kept for its capacity.
        std::shared_ptr<const Swell>       m_swell;   ///< The waves' swell, as m_shaped shapes it.
        std::optional<WaveShape>           m_shaped;  ///< What m_swell was made from; none before it is.
        std::shared_ptr<const SwellMoment> m_ahead;   ///< The next tick's swell, drawing on a worker.
};

} // namespace Vkm::Engine
