#pragma once

#include <memory>
#include <cstdint>

#include <glm/glm.hpp>

#include "config/gl_config.h"

namespace Core {
    class UniformBuffer;
}

namespace Engine {

/**
 * @brief A single shadow caster entry, std140 layout.
 *
 * The same struct is used for directional / spot (which sample the 2D array)
 * and point (which sample the cube array). lightSpace is only meaningful for
 * 2D-array maps; cube samples derive their lookup vector from the world-space
 * fragment position relative to the light, normalised by `radius`.
 *
 * Layout:
 *   lightSpace : mat4  (offset 0)   world -> light clip space  (2D casters only)
 *   params     : vec4  (offset 64)
 *     x = lightIndex      (index into the LightsBlock array)
 *     y = mapLayer        (layer in 2D array, or cube index in cube array)
 *     z = bias            (slope-scaled bias maximum)
 *     w = radius          (point lights only; range used to normalise depth)
 */
struct alignas(16) ShadowCasterGPUData {
    glm::mat4 lightSpace = glm::mat4(1.0f);
    glm::vec4 params     = glm::vec4(-1.0f, 0.0f, 0.005f, 0.0f);
};

/**
 * @brief Std140 UBO data with an array of shadow casters.
 *
 * Layout:
 *   casterCount : int   (offset 0,  std140 pads the trailing scalar to 16)
 *   casters[]   : ShadowCaster[MaxShadowCasters]
 */
struct alignas(16) ShadowUBOData {
    int  casterCount = 0;
    int  _pad[3]     = {0, 0, 0};
    ShadowCasterGPUData casters[GLConfig::Limits::MaxShadowCasters]{};
};

/**
 * @brief Per-frame shadow UBO upload and binding.
 *
 * Mirrors GLCamera / GLLights: eager allocation at construction so binding
 * point 3 is always valid, even if no caster is active or the shadow pass
 * is missing. Skips the GPU upload when the data is bytewise unchanged.
 */
class GLShadowData {
    public:
        GLShadowData();
        ~GLShadowData();

        GLShadowData(const GLShadowData&) = delete;
        GLShadowData& operator=(const GLShadowData&) = delete;
        GLShadowData(GLShadowData&&) = delete;
        GLShadowData& operator=(GLShadowData&&) = delete;

    public:
        /// Reset to "no casters" - the PBR shader will short-circuit shadow sampling.
        void clear();

        /// Append a caster entry. Returns true on success, false when the budget
        /// (MaxShadowCasters) is exceeded.
        bool addCaster(const ShadowCasterGPUData& caster);

        /// Upload the accumulated casters to the GPU (no-op when bytewise unchanged).
        void upload();

        /// Bind the UBO to the shadow binding point.
        void bind(uint32_t bindingPoint = GLConfig::UBOBindingPoints::Shadow) const;

    private:
        std::unique_ptr<Core::UniformBuffer> m_ubo;
        ShadowUBOData                        m_data{};
        ShadowUBOData                        m_lastData{};
};

} // namespace Engine
