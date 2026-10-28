#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "core/engine_config.h"

namespace Vkm::GL {
    class ShaderStorageBuffer;
}

namespace Vkm::Engine {

struct LightData;

/// A spot's map is drawn this much wider than its cone, so a filter disk at the cone's edge stays on it.
inline constexpr float SPOT_MAP_MARGIN = 1.1f;
/// The widest a spot's one map is drawn: wider, a perspective map degenerates.
inline const float SPOT_MAP_WIDEST = glm::radians(170.0f);

/**
 * @brief Whether @p light is a spot whose cone, with the margin, is wider than one map holds.
 *
 * Such a spot casts through a cube, as a point light does (GpuLight.bake.y), so its rim is shadowed.
 *
 * @param light A light of the frame.
 * @return True for a spot too wide for one map.
 */
bool spotCastsThroughCube(const LightData& light);

/**
 * @brief Whether @p light's shadow is drawn into a cube: a point light, or a spot too wide for one map.
 *
 * @param light A light of the frame.
 * @return True for a light whose shadow takes a cube.
 */
bool castsThroughCube(const LightData& light);

/**
 * @brief The field of view of the one map of spot @p light: its cone with the margin.
 *
 * Never zero, so a zero cone's projection does not divide by tan(0).
 *
 * @param light A spot of the frame that does not cast through a cube.
 * @return Radians, at most SPOT_MAP_WIDEST.
 */
float spotMapFov(const LightData& light);

/**
 * @brief A read-only view of a value per light of the frame, by its place in the list, -1 for none:
 *        its shadow slot (GLShadowData) or its lightmap mask channel (GLLightmaps::channels).
 *
 * Two members rather than std::span because the engine is C++17.
 */
struct LightSlots {
    const int* slots = nullptr;
    uint32_t   count = 0;

    /// @return The value of the light at @p index, or -1 when it has none.
    int operator[](uint32_t index) const { return index < count ? slots[index] : -1; }
};

/**
 * @brief std430 layout - must match Light in shaders/lights.glsl.
 *
 * Every slot is a vec4 to avoid drivers that fail to pack a trailing scalar
 * into a vec3's 4-byte tail (spec-legal but unreliable in practice).
 *
 *  - position:  xyz = world position,  w = type (encoded as float)
 *  - color:     xyz = RGB,             w = intensity
 *  - direction: xyz = world direction, w = attenuation radius
 *  - spot:      xy = the cone as a scale and offset on cos(angle to the axis):
 *               x = 1 / (cos inner - cos outer), y = -cos outer * x;
 *               z = tan of LightData::sourceRadius (directional; 0 otherwise),
 *               w = shadowSlot (-1 = no shadow): a cube's for a spot that casts through one
 *  - axisU:     xyz = half-right world axis (Rect/Disk), w = twoSided (0/1)
 *  - axisV:     xyz = half-up    world axis (Rect/Disk),
 *               w = sourceRadius (Point/Spot; 0 otherwise)
 *  - bake:      x = the lightmap mask channel its shadow on fixed meshes is read from, -1 for
 *               none (GLLightmaps); y = 1 for a spot that casts through a cube
 *               (spotCastsThroughCube); zw unused
 *
 * For punctual lights (Directional / Point / Spot) the axes' xyz are zero; the
 * shader's area-light branch is gated on type.
 */
struct GpuLight {
    glm::vec4 position;
    glm::vec4 color;
    glm::vec4 direction;
    glm::vec4 spot;
    glm::vec4 axisU;
    glm::vec4 axisV;
    glm::vec4 bake;
};

struct LightsBuffer {
    int      count;
    int      pad0, pad1, pad2;
    GpuLight lights[Config::MAX_LIGHTS];
};

/**
 * @brief GPU mirror of the frame's lights - the LightsBlock SSBO.
 *
 * update() packs each LightData into the std430 array the shaders iterate,
 * uploads it to the lights SSBO binding point, and skips the upload when the set
 * is unchanged from the previous frame. Lights past the cap are dropped. The
 * list lives in an SSBO rather than a UBO so it can grow past the UBO size
 * limit.
 */
class GLLights {
    public:
        GLLights();
        ~GLLights();

        GLLights(const GLLights& other) = delete;
        GLLights& operator=(const GLLights& other) = delete;

        GLLights(GLLights && other) = delete;
        GLLights& operator=(GLLights && other) = delete;

    public:
        /**
         * @brief Upload the frame's light list, tagging each with its shadow slot.
         *
         * @param lights       The frame's lights, capped at Config::MAX_LIGHTS.
         * @param shadowSlots  Each light's slot in the shadow atlas; a default one
         *                     means "none of them cast".
         * @param steady       For a capture: a flickering light shines steady, and one not in
         *                     captures (LightData::inCaptures) with nothing, keeping its place, so
         *                     what a probe bakes holds whatever the flame or the flash was doing.
         * @param maskChannels Each light's lightmap mask channel (GLLightmaps::channels); a default
         *                     one means none.
         */
        void update(
            const std::vector<LightData>& lights,
            LightSlots shadowSlots = {},
            bool steady = false,
            LightSlots maskChannels = {}
        );

    private:
        std::unique_ptr<Vkm::GL::ShaderStorageBuffer> m_ssbo;
        LightsBuffer                                 m_last{};
};

} // namespace Vkm::Engine
