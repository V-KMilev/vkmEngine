#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "storage/light/gl_light_slots.h"
#include "system/render/render_limits.h"

namespace Vkm::OpenGL {
    class ShaderStorageBuffer;
}

namespace Vkm::Engine {

struct LightData;

/**
 * @brief std430 layout - must match Light in shaders/opengl/include/lights.glsl.
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
 *  - bake:      x = the lightmap mask channel its shadow on fixed meshes is read from, -1 for
 *               none (GLLightmaps); y = 1 for a spot that casts through a cube
 *               (spotCastsThroughCube); z = sourceRadius (Point/Spot; 0 otherwise); w unused
 */
struct GpuLight {
    glm::vec4 position;
    glm::vec4 color;
    glm::vec4 direction;
    glm::vec4 spot;
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
        std::unique_ptr<Vkm::OpenGL::ShaderStorageBuffer> m_ssbo;
        LightsBuffer                                      m_last{};
};

} // namespace Vkm::Engine
