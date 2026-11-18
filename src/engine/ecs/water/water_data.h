#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/component/landscape/water.h"
#include "ecs/entity.h"
#include "ecs/water/wave_frame.h"
#include "ecs/water/wave_surface.h"

namespace Vkm::Engine {

/**
 * @brief The water at a point, as WaterData::at answers it.
 */
struct WaterSample {
    bool      found   = false;               ///< Whether the point is in water, or over a surface.
    bool      wet     = false;               ///< Whether the point lies in it: under its surface.
    float     surface = 0.0f;                ///< World height of the water's surface over the point.
    glm::vec3 normal  = {0.0f, 1.0f, 0.0f};  ///< The surface's, unit.
    glm::vec3 flow    = glm::vec3(0.0f);     ///< How fast the water runs there, world m/s.
    EntityId  body;                          ///< The surface's entity.
};

/**
 * @brief A cell of a body's grid: its surface at rest over the cell's middle.
 */
struct WaterCell {
    float     height = 0.0f;                ///< Above the body's origin in its grid, in the world from
                                            ///< sample(); none dry.
    bool      wet    = false;
    glm::vec3 along  = glm::vec3(0.0f);     ///< Unit: the way the mesh's +U runs, which it flows along.
    glm::vec3 normal = {0.0f, 1.0f, 0.0f};  ///< Unit, up.
};

/**
 * @brief A body of water: an entity's Water and the surface its mesh draws, laid on a grid over the
 *        XZ plane.
 *
 * The grid is laid about the body's origin, so water that moves without turning keeps it: raising a
 * sea lays nothing again, and the grid is the same whichever place it was first laid at.
 */
struct WaterBody {
    EntityId               entity;
    Water                  water;                      ///< Its values, as they stand now.
    glm::vec3              origin  = glm::vec3(0.0f);  ///< Where its model puts its mesh's origin.
    glm::vec2              corner  = glm::vec2(0.0f);  ///< The grid's least X and Z, from the origin.
    float                  cell    = 1.0f;             ///< Meters across a cell.
    uint32_t               columns = 0;                ///< Cells along X.
    uint32_t               rows    = 0;                ///< Cells along Z.
    std::vector<WaterCell> cells;                      ///< Rows of Z, X across each.

    glm::mat3 turn        = glm::mat3(0.0f);  ///< What the grid was laid from: laid again when any changes. A
                                              ///< move alone changes the origin.
    uint64_t  meshVersion = 0;
    uint64_t  meshUid     = 0;

    /**
     * @brief The surface at rest over world point @p x, @p z, read between the cells' middles.
     *
     * @param x World X.
     * @param z World Z.
     * @param out Filled where the body lies over the point.
     * @return Whether it does.
     */
    bool sample(float x, float z, WaterCell& out) const;
};

/**
 * @brief The scene's water at the moment being computed: its bodies, the waves that move them, and
 *        what lies at any point of them. A scene with no Water has neither.
 *
 * Held by the scene (Scene::water) and built there by WaterSystem once a tick and once a frame. It is
 * computed, never saved: a world that arrives brings an empty one, laid by the next build.
 */
class WaterData {
    public:
        WaterData()  = default;
        ~WaterData() = default;

        WaterData(const WaterData& other) = default;
        WaterData& operator=(const WaterData& other) = default;

        WaterData(WaterData && other) = default;
        WaterData& operator=(WaterData && other) = default;

    public:
        /**
         * @brief The water at @p point: of the bodies it lies in - under the surface and above the
         *        bottom - the one whose bottom is highest, then the nearest surface; else the nearest
         *        surface below it, dry.
         *
         * A body's surface is its mesh's at rest, moved by its share of the waves (Water::waves); its
         * bottom Water::depth under its rest, or none. So a lake above the sea floats what is in it, a
         * pool far below the sea hides none of it, and a pool on a deck leaves what is under the deck
         * dry.
         *
         * @param point A world point.
         * @return What is there; `found` false where the point is in no water and over no surface.
         */
        WaterSample at(const glm::vec3& point) const;

        const std::vector<WaterBody>& bodies() const { return m_bodies; }

        /// The waves at this moment, for what samples them itself; still with no Water in the scene.
        const WaveFrame& waves() const { return m_waves; }

    private:
        friend class WaterSystem;

    private:
        std::vector<WaterBody> m_bodies;
        WaveFrame              m_waves;  ///< As WaterSystem built them; still with no Water in the scene.
};

} // namespace Vkm::Engine
