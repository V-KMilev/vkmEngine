#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "core/engine_config.h"

namespace Vkm::GL {
    class UniformBuffer;
}

namespace Engine {
    struct RenderView;
    struct LightData;
}

namespace Engine {

// Shadow slots are only ever queried for lights that made the GPU light list.
constexpr uint32_t SHADOW_MAX_TRACKED_LIGHTS = Config::MAX_LIGHTS;

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
 * @brief The casters one shadow job rasterises: indices into RenderView::shadowCasters.
 *
 * Sorted by mesh id so the pass can walk it as contiguous per-mesh runs and
 * issue one instanced draw each.
 */
struct ShadowCasterBatch {
    std::vector<uint32_t> order;
};

/**
 * @brief Per-task workspace for the cull, owned by GLShadowData and reused.
 *
 * One instance per job so tasks never share, and kept between frames so a steady
 * shadow plan allocates nothing.
 */
struct CullScratch {
    std::vector<uint32_t> survivors;  ///< Frustum survivors, before mesh grouping.
    std::vector<uint32_t> counts;     ///< Per-mesh histogram, then the scatter cursor.
};

/**
 * @brief Builds the frame's shadow plan and owns the ShadowBlock UBO.
 *
 * build() assigns atlas slots to shadow-casting lights, fits the directional
 * cascades / spot frusta / point cube matrices, records the render jobs, and
 * culls the caster list against each one. slotForLight() feeds GLLights (the
 * per-light shadowSlot); the jobs and their batches feed GLShadowPass. Call
 * build() before GLLights::update and uploadAndBind().
 *
 * Culling lives here rather than in the pass because it is not a GL concern and
 * because doing it per tile, inline, was the single most expensive thing in the
 * frame: the caster set is scene-wide, so each cascade, spot and cube face
 * re-scanned every caster while the render thread waited. Hoisting it lets all
 * of it run on the thread pool, and lets a point light reject most of the scene
 * once against its bounding sphere instead of six times against its faces.
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
         * @brief Casters surviving the cull for 2D job @p jobIndex.
         *
         * @param jobIndex Index into jobs2D().
         * @return Mesh-sorted indices into RenderView::shadowCasters.
         */
        const ShadowCasterBatch& batch2D(size_t jobIndex) const { return m_batches2D[jobIndex]; }

        /**
         * @brief Casters surviving the cull for one face of a cube job.
         *
         * @param jobIndex Index into jobsCube().
         * @param face     Cube face, 0-5.
         * @return Mesh-sorted indices into RenderView::shadowCasters.
         */
        const ShadowCasterBatch& batchCube(size_t jobIndex, uint32_t face) const {
            return m_batchesCube[jobIndex * 6 + face];
        }

        /**
         * @brief Upload the ShadowBlock UBO and bind it to its binding point.
         */
        void uploadAndBind();

    private:
        /**
         * @brief Cull the caster list against every job recorded this frame.
         *
         * Runs on the thread pool: each cascade, spot and cube face is an
         * independent scan writing to its own batch, so there is nothing to
         * synchronise. Point lights reject against the light's bounding sphere
         * once first, and their six faces refine that survivor set instead of
         * re-scanning the scene.
         *
         * @param view The frame's render view, supplying shadowCasters.
         */
        void cullCasters(const RenderView& view);

        /**
         * @brief Camera frustum corners + view-space depth span, shared by the cascade fit.
         */
        struct CameraFrustum {
            glm::vec3 nearCorners[4];
            glm::vec3 farCorners[4];
            float nearDepth = 0.0f;
            float farDepth  = 0.0f;
        };

        /**
         * @brief Fit one shadow-casting light into the next free atlas slot(s) for its
         * type, writing its GPU entry + render job. next2D/nextCube advance.
         */
        void fitDirectional(const LightData& light, uint32_t lightIndex,
                            const CameraFrustum& cam, uint32_t& next2D, bool& haveSun);
        void fitSpot(const LightData& light, uint32_t lightIndex, uint32_t& next2D);
        void fitPoint(const LightData& light, uint32_t lightIndex, uint32_t& nextCube);

    private:
        std::unique_ptr<Vkm::GL::UniformBuffer> m_ubo;
        ShadowUBOData m_data{};
        ShadowUBOData m_last{};

        int      m_lightSlot[SHADOW_MAX_TRACKED_LIGHTS];
        uint32_t m_lightCount = 0;
        uint32_t m_shadowRes = 0;  ///< Atlas tile resolution this frame, for world-texel bias sizing.

        std::vector<Shadow2DJob>   m_jobs2D;
        std::vector<ShadowCubeJob> m_jobsCube;

        // Cull results, index-aligned with the job lists above (cube batches are
        // flattened six-per-job). Kept across frames and only cleared, never
        // freed, so a steady shadow plan stops allocating after the first frame.
        std::vector<ShadowCasterBatch> m_batches2D;
        std::vector<ShadowCasterBatch> m_batchesCube;

        // Scratch for the cull, kept alive between frames for the same reason.
        std::vector<uint32_t>              m_allCasters;      ///< Identity index list: every caster is a 2D-job candidate.
        std::vector<std::vector<uint32_t>> m_cubeCandidates;  ///< Per point light, the casters its sphere reaches.
        std::vector<uint32_t>              m_meshKeys;        ///< Mesh id per caster, flattened for the grouping pass.
        uint32_t                           m_keyCount = 0;    ///< Histogram size: highest mesh id in use, plus one.
        std::vector<CullScratch>           m_scratch;         ///< One workspace per cull task.
};

} // namespace Engine
