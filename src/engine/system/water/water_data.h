#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"

namespace Vkm::Engine {

struct OceanData;

/// The water at a point, as WaterData::at answers it.
struct WaterSample {
    bool      found   = false;  ///< Whether the point is in water, or over a surface.
    bool      wet     = false;  ///< Whether the point lies in it: under its surface.
    float     surface = 0.0f;   ///< World height of the water's surface over the point.
    glm::vec3 normal  = {0.0f, 1.0f, 0.0f};  ///< The surface's, unit.
    glm::vec3 flow    = glm::vec3(0.0f);     ///< How fast the water runs there, world m/s.
    EntityId  body;                          ///< The surface's entity; null for the sea.
};

/// A cell of a body's grid: the surface over its middle.
struct WaterCell {
    float     height = 0.0f;  ///< World height; none when `wet` is false.
    bool      wet    = false;
    glm::vec3 flow   = glm::vec3(0.0f);           ///< World m/s.
    glm::vec3 normal = {0.0f, 1.0f, 0.0f};        ///< Unit, up.
};

/**
 * @brief A body of water other than the sea - a lake, a river, a pool: an entity's mesh drawn with
 *        a Body water material (MaterialAsset::water), its surface laid on a grid over the XZ plane.
 */
struct WaterBody {
    EntityId               entity;
    glm::vec2              corner  = glm::vec2(0.0f);  ///< World X and Z of the grid's least corner.
    float                  cell    = 1.0f;             ///< Metres across a cell.
    uint32_t               columns = 0;                ///< Cells along X.
    uint32_t               rows    = 0;                ///< Cells along Z.
    std::vector<WaterCell> cells;                      ///< Rows of Z, X across each.
    float                  depth   = 0.0f;             ///< Metres below the surface it reaches.

    /// What the grid was laid from: drawn again when any of them changes.
    glm::mat4 model           = glm::mat4(0.0f);
    uint64_t  meshVersion     = 0;
    uint64_t  materialVersion = 0;
    uint64_t  meshUid         = 0;
    uint64_t  materialUid     = 0;

    /**
     * @brief The surface over world point @p x, @p z, read between the cells' middles.
     *
     * @param x World X.
     * @param z World Z.
     * @param out Filled where the body lies over the point.
     * @return Whether it does.
     */
    bool sample(float x, float z, WaterCell& out) const;
};

/**
 * @brief The scene's water at the moment being computed (WaterSystem): its bodies and its sea, and
 *        what lies at any point of them.
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
         * @brief The water at @p point: the water it is in - a body it lies under by less than the
         *        body's depth, before the sea it lies under - else the nearest surface below it.
         *
         * The sea is water only within its surfaces' boxes (a Sea-material mesh's), widened by as far
         * as its waves carry.
         *
         * Of several, the nearest surface. So a lake above the sea floats what is in it, a pool far
         * below the sea hides none of it, and a pool on a deck leaves what is under the deck dry.
         *
         * @param point A world point.
         * @return What is there; `found` false where the point is in no water and over no surface.
         */
        WaterSample at(const glm::vec3& point) const;

        const std::vector<WaterBody>& bodies() const { return m_bodies; }

    private:
        friend class WaterSystem;

        /**
         * @brief Whether @p point lies over a surface of the sea (m_seaSpans).
         *
         * @param point A world point.
         * @return Whether the sea is water there.
         */
        bool overSea(const glm::vec3& point) const;

    private:
        std::vector<WaterBody> m_bodies;
        /// The world XZ boxes the sea's surfaces cover, least corner then most: the sea is water only
        /// over them, and as far past their edges as its waves carry.
        std::vector<glm::vec4> m_seaSpans;
        const OceanData*       m_ocean = nullptr;  ///< The sea, as OceanSystem left it; null for none.
};

} // namespace Vkm::Engine
