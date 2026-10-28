#pragma once

#include <vector>

#include "core/system.h"
#include "resource/asset/material_asset.h"
#include "system/water/water_data.h"

namespace Vkm::Engine {

struct MeshAsset;

/**
 * @brief Finds the scene's water and floats what is in it.
 *
 * Every Mesh drawn with a Body water material is a body of water (WaterBody): its surface, as its
 * entity draws it, is laid on a grid of heights and flows - again whenever its matrix, its mesh or
 * its material changes. Every Mesh drawn with a Sea material bounds where the sea is water.
 * Published with the sea (OceanSystem's) as FrameContext::water once a tick and once a frame, for
 * gameplay. Each tick it then floats every Buoyancy body on the water it is in. At Simulation, after
 * OceanSystem and ahead of BehaviorSystem and physics.
 */
class WaterSystem : public System {
    public:
        /// Metres across a grid cell at the least, and the most cells across a body: a larger body's
        /// cells widen.
        static constexpr float    LEAST_CELL = 0.25f;
        static constexpr uint32_t MOST_CELLS = 512;

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

        /// A float is a function of the world: a replayed tick finds the bodies of water again and
        /// lifts it on them, and on the sea OceanSystem built for it.
        bool isReplayed() const override { return true; }

        /**
         * @brief Lay @p mesh's surface, drawn by @p model, on @p body's grid.
         *
         * @param body     Filled: its grid and cells.
         * @param mesh     The surface's geometry.
         * @param material Its material, for the flow.
         * @param model    Its world matrix.
         */
        static void layOut(
            WaterBody& body,
            const MeshAsset& mesh,
            const MaterialAsset& material,
            const glm::mat4& model
        );

    private:
        /// Find the bodies and the sea's surfaces, lay out any body changed, and publish them with the sea.
        void build(FrameContext& ctx);

        /// Float every Buoyancy body this end simulates on the water it is in.
        void floatBodies(FrameContext& ctx, float dt);

    private:
        WaterData                   m_water;
        std::vector<WaterBody>      m_kept;    ///< build()'s next list of bodies, kept for its capacity.
        std::vector<MaterialHandle> m_waters;  ///< build()'s materials that are water; scratch.
};

} // namespace Vkm::Engine
