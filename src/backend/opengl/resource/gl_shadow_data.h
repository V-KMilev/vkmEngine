#pragma once

#include <memory>
#include <cstdint>

#include <glm/glm.hpp>

#include "config/gl_config.h"
#include "core/engine_config.h"

namespace Core {
    class UniformBuffer;
}

namespace Engine {

/**
 * @brief 2D shadow caster entry (std140 layout) - directional + spot.
 *
 * Sampled as sampler2DArrayShadow; atlas layer = the entry's index, so no
 * explicit layer field is needed.
 *
 *   lightSpace : mat4  (offset 0)   world -> light clip space
 *   params.x   : float (offset 64)  depth-compare bias
 *   params.yzw : float (offset 68)  unused/pad
 */
struct alignas(16) Shadow2DCasterGPU {
    glm::mat4 lightSpace = glm::mat4(1.0f);
    glm::vec4 params     = glm::vec4(0.005f, 0.0f, 0.0f, 0.0f);
};

/**
 * @brief Cube shadow caster entry (std140 layout) - point lights.
 *
 * Sampled as samplerCubeArrayShadow; the cube index equals the entry's index.
 *
 *   params.x : float  bias
 *   params.y : float  light range (cube far plane, used to derive projected depth)
 */
struct alignas(16) ShadowCubeCasterGPU {
    glm::vec4 params = glm::vec4(0.005f, 1.0f, 0.0f, 0.0f);
};

/**
 * @brief Shadow UBO data (std140).
 *
 * Two separate caster arrays - 2D (directional + spot) and cube (point) -
 * indexed directly by the slot the light is carrying in its GPU data.
 */
struct alignas(16) ShadowUBOData {
    int count2D     = 0;
    int countCube   = 0;
    int csmBaseSlot = -1;   ///< First 2D layer of cascade 0 (-1 = no CSM); was _pad0
    int csmCount    = 0;    ///< Active cascade count for the sun;        was _pad1
    Shadow2DCasterGPU   casters2D  [Config::MaxShadowCasters2D]{};
    ShadowCubeCasterGPU castersCube[Config::MaxShadowCastersCube]{};
};

/**
 * @brief Per-frame shadow UBO. Owned by GLView, populated by GLShadowPass.
 *
 * Allocation is eager so binding point 3 is always valid even if no caster
 * is active or the shadow pass is missing.
 */
class GLShadowData {
    public:
        GLShadowData();
        ~GLShadowData();

        GLShadowData(const GLShadowData& other) = delete;
        GLShadowData& operator=(const GLShadowData& other) = delete;

        GLShadowData(GLShadowData && other) = delete;
        GLShadowData& operator=(GLShadowData && other) = delete;

    public:
        /// Reset both caster counts to 0. Stale array tails are inert - the
        /// shader only reads the prefix indicated by count2D / countCube.
        void clear();

        /// Write a 2D caster at the given slot (0..MaxCasters2D-1).
        void setCaster2D(uint32_t slot, const Shadow2DCasterGPU& caster);

        /// Write a cube caster at the given slot (0..MaxCastersCube-1).
        void setCasterCube(uint32_t slot, const ShadowCubeCasterGPU& caster);

        /// Set the active counts. Must be called after all setCaster*() calls.
        void setCounts(uint32_t count2D, uint32_t countCube);

        /// Record the directional cascade layout (base 2D layer + count).
        void setCSM(int baseSlot, int count);

        /// Upload to GPU and bind to the shadow binding point.
        void uploadAndBind();

    private:
        std::unique_ptr<Core::UniformBuffer> m_ubo;
        ShadowUBOData                        m_data{};
};

} // namespace Engine
