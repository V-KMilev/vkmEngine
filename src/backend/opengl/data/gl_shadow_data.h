#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "core/engine_config.h"

namespace Core {
    class UniformBuffer;
}

namespace Engine {
    struct RenderView;
}

namespace Engine {

static constexpr uint32_t SHADOW_MAX_TRACKED_LIGHTS = 64;

/**
 * @brief One 2D shadow caster (std140) - a directional cascade or a spot map.
 *
 *   lightVP : world -> light clip space
 *   atlas   : xy = tile UV offset, zw = tile UV scale (sample = offset + uv*scale)
 *   params  : x = depth-compare bias, y = world size of one shadow texel
 */
struct alignas(16) Shadow2DGPU {
    glm::mat4 lightVP = glm::mat4(1.0f);
    glm::vec4 atlas   = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    glm::vec4 params  = glm::vec4(0.001f, 0.0f, 0.0f, 0.0f);
};

/**
 * @brief One cube shadow caster (std140) - a point light.
 *
 *   posRange : xyz = light world position, w = far range (depth normaliser)
 *   params   : x = bias
 */
struct alignas(16) ShadowCubeGPU {
    glm::vec4 posRange = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 params   = glm::vec4(0.001f, 0.0f, 0.0f, 0.0f);
};

/**
 * @brief ShadowBlock UBO (std140) - must match the block in shaders/forward.
 *
 * camForward + cascadeSplits drive directional cascade selection by view depth;
 * csmBase/csmCount mark the sun's cascade run inside s2d. Spot maps take the
 * remaining s2d slots; point lights use scube. A light carries its slot in the
 * lights UBO (GpuLight.spot.w), so no per-light index lives here.
 */
struct alignas(16) ShadowUBOData {
    glm::vec4 camForward    = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
    glm::vec4 cascadeSplits = glm::vec4(0.0f);  ///< view-space far depth per cascade
    int csmBase  = -1;
    int csmCount = 0;
    int _pad0 = 0;
    int _pad1 = 0;
    Shadow2DGPU   s2d  [Config::MAX_SHADOW_CASTERS_2D]{};
    ShadowCubeGPU scube[Config::MAX_SHADOW_CASTERS_CUBE]{};
};

/**
 * @brief A 2D depth render job: rasterise shadow casters into atlas tile `slot`.
 */
struct Shadow2DJob {
    glm::mat4 lightVP; ///< The light view-projection matrix.
    uint32_t  slot;    ///< The atlas tile slot.
};

/**
 * @brief A cube depth render job: six face matrices for the point light at `slot`.
 */
struct ShadowCubeJob {
    glm::mat4 faceVP[6]; ///< The six face view-projection matrices.
    glm::vec3 pos;       ///< The position of the point light.
    float     range;     ///< The range of the point light.
    uint32_t  slot;      ///< The atlas tile slot.
};

/**
 * @brief Builds the frame's shadow plan and owns the ShadowBlock UBO.
 *
 * build() assigns atlas slots to shadow-casting lights, fits the directional
 * cascades / spot frusta / point cube matrices, and records the render jobs.
 * slotForLight() feeds GLLights (the per-light shadowSlot); the jobs feed
 * GLShadowPass. Call build() before GLLights::update and uploadAndBind().
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
        /**
         * @brief Plan the frame's shadows: assign atlas slots, fit the cascade /
         *        spot / cube matrices, and record the render jobs.
         *
         * Call before GLLights::update (which reads slotForLight) and before
         * uploadAndBind.
         */
        void build(const RenderView& view);

        /**
         * @brief Packed shadowSlot for the light at @p lightIndex (-1 = no shadow).
         */
        int slotForLight(uint32_t lightIndex) const;

        /**
         * @brief The 2D depth jobs (directional cascades + spots) for this frame.
         */
        const std::vector<Shadow2DJob>&   jobs2D()   const { return m_jobs2D; }

        /**
         * @brief The cube depth jobs (point lights) for this frame.
         */
        const std::vector<ShadowCubeJob>& jobsCube() const { return m_jobsCube; }

        /**
         * @brief Upload the ShadowBlock UBO and bind it to its binding point.
         */
        void uploadAndBind();

    private:
        std::unique_ptr<Core::UniformBuffer> m_ubo;
        ShadowUBOData m_data{};
        ShadowUBOData m_last{};

        int      m_lightSlot[SHADOW_MAX_TRACKED_LIGHTS];
        uint32_t m_lightCount = 0;

        std::vector<Shadow2DJob>   m_jobs2D;
        std::vector<ShadowCubeJob> m_jobsCube;
};

} // namespace Engine
