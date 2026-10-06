#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "core/engine_config.h"
#include "frame/gl_lights.h"
#include "platform/threading/thread_pool.h"

namespace Vkm::GL {
    class UniformBuffer;
}

namespace Vkm::Engine {

class GLView;
struct RenderView;
struct LightData;

/**
 * @brief An up axis for a light looking along @p dir, never parallel to it.
 *
 * So a near-vertical light never makes a degenerate lookAt.
 *
 * @param dir The direction the light looks along, normalized.
 * @return World +Y, or +Z when @p dir is within a few degrees of vertical.
 */
glm::vec3 stableUp(const glm::vec3& dir);

/**
 * @brief One 2D shadow caster (std140) - a directional cascade or a spot map.
 *
 *   lightVP : world -> light clip space
 *   atlas   : xy = tile UV offset, zw = tile UV scale (sample = offset + uv*scale)
 *   params  : x = Light::shadowBias (a cascade's in depth; a spot's a fraction of its range, as
 *             a point light's: rayBias in shaders/shadows.glsl), y = world size of a shadow texel
 *             (a spot's at its range), z = far plane (the world depth an ortho tile's 0..1 spans;
 *             a spot's range), w = source size for the penumbra filter, 0 = hard 3x3 (a
 *             cascade's: tan of the sun's angular radius; a spot's: source radius over the
 *             width the map spans one metre out)
 *   shape   : x = normal-offset bias in this tile's texels (LightData::shadowNormalBias),
 *             y = near plane of a perspective tile (spot), 0 if orthographic (cascade),
 *             z = tan of a spot's half field of view
 */
struct alignas(16) Shadow2DGPU {
    glm::mat4 lightVP = glm::mat4(1.0f);
    glm::vec4 atlas   = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    glm::vec4 params  = glm::vec4(0.001f, 0.0f, 0.0f, 0.0f);
    glm::vec4 shape   = glm::vec4(1.5f, 0.0f, 0.0f, 0.0f);
};

/**
 * @brief One cube shadow caster (std140) - a point light.
 *
 *   posRange : xyz = light world position, w = range, the faces' far plane
 *   params   : x = Light::shadowBias, a fraction of the range (rayBias in
 *              shaders/shadows.glsl); y = the faces' near plane
 */
struct alignas(16) ShadowCubeGPU {
    glm::vec4 posRange = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 params   = glm::vec4(0.001f, 0.0f, 0.0f, 0.0f);
};

/**
 * @brief ShadowBlock UBO (std140) - must match ShadowBlock in shaders/shadows.glsl.
 *
 * cascadeSplits pick a cascade by view depth; csmBase/csmCount mark the sun's run in s2d, spots
 * take the rest, point lights scube. A light carries its slot in the lights SSBO
 * (GpuLight.spot.w).
 */
struct alignas(16) ShadowUBOData {
    glm::vec4 cascadeSplits = glm::vec4(0.0f);  ///< View-space far depth per cascade.
    int csmBase  = -1;
    int csmCount = 0;
    int pad0     = 0;
    int pad1     = 0;
    Shadow2DGPU   s2d  [Config::MAX_SHADOW_CASTERS_2D]{};
    ShadowCubeGPU scube[Config::MAX_SHADOW_CASTERS_CUBE]{};
};

/**
 * @brief A 2D depth render job: rasterise shadow casters into atlas tile `slot`.
 */
struct Shadow2DJob {
    glm::mat4 lightVP;
    uint32_t  slot;
    /**
     * @brief Whether this is one of the sun's cascades rather than a spot's map.
     *
     * A cascade is never kept between frames: it moves with the camera. It draws with depth
     * clamped, so a caster nearer the sun than its near plane is flattened onto it, not clipped.
     */
    bool      cascade;
};

/**
 * @brief A cube depth render job: six face matrices for the point light at `slot`.
 */
struct ShadowCubeJob {
    glm::mat4 faceVP[6];
    glm::vec3 pos;
    float     range;
    uint32_t  slot;
};

/**
 * @brief One instanced draw of a shadow batch: casters sharing a mesh and a pose state.
 */
struct ShadowRun {
    uint32_t first = 0;  ///< Where the run starts in ShadowCasterBatch::order.
    uint32_t count = 0;  ///< Casters in it.
    /**
     * @brief What its casters share: `group * slots + slot`.
     *
     * The slot is the mesh id for opaque casters; alpha-masked ones take a slot per (material,
     * mesh) past every mesh id, so a run shares the material cutting its shadow. The group is the
     * high digit (static, skinned as stored, skinned and posed), so runs come in that order and
     * neither vertex layout nor pose alternates within a tile.
     */
    uint32_t key   = 0;
    bool     posed = false;  ///< Whether the frame posed its casters: the last group of `key`.
};

/**
 * @brief The casters one shadow job rasterises: indices into RenderView::objects.
 *
 * Grouped by mesh (a masked caster by mesh and material), then by whether posed, so the pass
 * issues one instanced draw per run the cull found.
 */
struct ShadowCasterBatch {
    std::vector<uint32_t>  order;
    std::vector<ShadowRun> runs;  ///< The groups of `order`, in order.
    /**
     * @brief What the tile drawn from this batch looks like, as a hash.
     *
     * Of the culling matrix and each survivor's mesh id, geometry upload (GLMesh::uploadId; for a
     * cutout also its material's and albedo map's) and model. Equal signatures draw the same
     * picture, so the atlas keeps the tile (GLShadowAtlas::tileHolds). Zero always draws: the
     * job is not cacheable, or a survivor is posed and its picture is not in its model matrix.
     */
    uint64_t signature = 0;
};

/**
 * @brief Per-task workspace for the cull, owned by GLShadowData and reused.
 *
 * One per job so tasks never share; kept between frames so a steady plan allocates nothing.
 */
struct CullScratch {
    std::vector<uint32_t> survivors;  ///< Frustum survivors, as caster positions, before mesh grouping.
    std::vector<uint32_t> counts;     ///< Per-key histogram, then the scatter cursor.
};

/**
 * @brief Builds the frame's shadow plan and owns the ShadowBlock UBO.
 *
 * Culling lives here, not in the pass, because it is not a GL concern and the caster set is
 * scene-wide: a task per tile and face runs on the thread pool, behind a serial pass rejecting
 * most of the scene once against a point light's sphere instead of six times against its faces.
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
         * @brief Plan the frame's shadows: assign atlas slots, fit the matrices, record the jobs.
         *
         * Call before GLLights::update (which reads lightSlots()) and uploadAndBind. The caster
         * cull it forks is left running; the batches are unreadable until finishCull.
         *
         * Atlas slots run out before lights do (Config::NUM_CASCADES of the
         * Config::MAX_SHADOW_CASTERS_2D tiles go to the sun). Lights go in entity-slot order,
         * since `view.lights` is SparseSet packing order and destroying an unrelated light would
         * hand a shadow to another mid-session (tie-break: findLowestSlot, ecs/scene.h). The sun
         * goes first whatever its slot: spots ahead of it could leave too short a run for its
         * consecutive cascades.
         *
         * @param view    Its lights and its camera.
         * @param glView  Already synced: a caster's geometry upload is part of its tile's look.
         * @param tileRes The tile edge as the atlas built it (GLShadowAtlas::tileResolution), not
         *                as asked: the normal-offset bias sizes a shadow texel from it.
         */
        void build(const RenderView& view, const GLView& glView, uint32_t tileRes);

        /**
         * @brief Upload the ShadowBlock UBO and bind it to its binding point.
         */
        void uploadAndBind();

        /**
         * @brief Join the caster cull that build forked.
         *
         * Returns at once when nothing is in flight. Call before reading batch2D or batchCube,
         * after the work the frame can do meanwhile.
         */
        void finishCull();

        /**
         * @brief The packed shadow slot of each light this frame, -1 for none.
         *
         * Parallel to `RenderView::lights` as far as it goes; a light past the end has none.
         *
         * @return A view of the slots, in light order; valid until the next build().
         */
        LightSlots lightSlots() const { return {m_lightSlot, m_lightCount}; }

        /**
         * @brief The 2D depth jobs (directional cascades + spots) for this frame.
         *
         * @return The jobs, in atlas-slot order; valid until the next build().
         */
        const std::vector<Shadow2DJob>& jobs2D() const { return m_jobs2D; }

        /**
         * @brief The cube depth jobs (point lights) for this frame.
         *
         * @return The jobs, in cube-slot order; valid until the next build().
         */
        const std::vector<ShadowCubeJob>& jobsCube() const { return m_jobsCube; }

        /**
         * @brief Casters surviving the cull for 2D job @p jobIndex.
         *
         * @param jobIndex Index into jobs2D().
         * @return The batch: its object indices grouped by key, and a run per key.
         */
        const ShadowCasterBatch& batch2D(size_t jobIndex) const { return m_batches2D[jobIndex]; }

        /**
         * @brief Casters surviving the cull for one face of a cube job.
         *
         * @param jobIndex Index into jobsCube().
         * @param face     Cube face, 0-5.
         * @return The batch: its object indices grouped by key, and a run per key.
         */
        const ShadowCasterBatch& batchCube(size_t jobIndex, uint32_t face) const {
            return m_batchesCube[jobIndex * 6 + face];
        }

    private:
        /**
         * @brief The cull's task body, lent to the thread pool's batch.
         *
         * A Batch borrows its task, so the body must outlive the fork; a member does, where a
         * lambda local to cullCasters would not.
         */
        struct CullTaskBody {
            GLShadowData* self;

            void operator()(size_t task) const { self->cullTask(task); }
        };

        /**
         * @brief Camera frustum corners + view-space depth span, shared by the cascade fit.
         */
        struct CameraFrustum {
            glm::vec3 nearCorners[4];
            glm::vec3 farCorners[4];
            float nearDepth = 0.0f;
            float farDepth  = 0.0f;
        };

    private:
        /**
         * @brief Fork the cull of the caster list against every job recorded this frame.
         *
         * Each cascade, spot and cube face is an independent scan into its own batch, so nothing
         * synchronises; a point light rejects against its sphere once and its faces refine that.
         * The tasks read only the view's objects, held still for the frame, and write only this
         * object's batches and scratch, so the main thread builds on until finishCull joins.
         *
         * @param view   Supplies the objects and the casters among them.
         * @param glView For each caster's upload id.
         */
        void cullCasters(const RenderView& view, const GLView& glView);

        /**
         * @brief Cull one job's casters into its batch: a 2D tile, or one cube face.
         *
         * @param task The 2D jobs first, then six faces per cube job.
         */
        void cullTask(size_t task);

        /**
         * @brief Fit the sun's cascades into the next free 2D atlas slots.
         *
         * Writes their GPU entries and jobs. A tile carries its light's source size for the
         * penumbra filter; zero is a hard shadow, on the 3x3 path.
         *
         * @param light      The directional light.
         * @param lightIndex Its index in the frame's light list.
         * @param cam        The camera frustum the cascades split.
         * @param next2D     The next free 2D slot, advanced past the cascades.
         * @param haveSun    Whether a sun already took the cascades; set once one has.
         */
        void fitDirectional(
            const LightData& light,
            uint32_t lightIndex,
            const CameraFrustum& cam,
            uint32_t& next2D,
            bool& haveSun
        );

        /**
         * @brief Fit a spot light into the next free 2D atlas slot.
         *
         * @param light      The spot light.
         * @param lightIndex Its index in the frame's light list.
         * @param next2D     The next free 2D slot, advanced past the one taken.
         */
        void fitSpot(const LightData& light, uint32_t lightIndex, uint32_t& next2D);

        /**
         * @brief Fit a point light into the next free shadow cube.
         *
         * @param light      The point light.
         * @param lightIndex Its index in the frame's light list.
         * @param nextCube   The next free cube, advanced past the one taken.
         */
        void fitPoint(const LightData& light, uint32_t lightIndex, uint32_t& nextCube);

    private:
        std::unique_ptr<Vkm::GL::UniformBuffer> m_ubo;
        ShadowUBOData m_data{};
        ShadowUBOData m_last{};

        int      m_lightSlot[Config::MAX_LIGHTS];  ///< Per light that made the GPU list, its slot.
        uint32_t m_lightCount = 0;

        uint32_t m_refused      = 0;      ///< Shadow casters this frame that found no free atlas tile.
        bool     m_budgetLogged = false;  ///< Whether that refusal has been reported; cleared once it stops.

        /// The atlas's own tile resolution this frame, for world-texel bias sizing.
        uint32_t m_shadowRes = 0;

        /**
         * @brief This frame's shadow-casting lights, in entity-slot order.
         *
         * A member for its capacity, like the cull scratch below: refilled every frame.
         */
        std::vector<uint32_t> m_shadowOrder;

        std::vector<Shadow2DJob>   m_jobs2D;
        std::vector<ShadowCubeJob> m_jobsCube;

        // Cull results, index-aligned with the jobs (cube batches six per job). Only ever cleared,
        // so a steady plan stops allocating after the first frame.
        std::vector<ShadowCasterBatch> m_batches2D;
        std::vector<ShadowCasterBatch> m_batchesCube;

        // Scratch for the cull, kept alive between frames for the same reason.
        /// Every caster's position in the scene list's prefix: every 2D job's candidates.
        std::vector<uint32_t>              m_casters;
        /// Per point light, the positions of the casters its sphere reaches.
        std::vector<std::vector<uint32_t>> m_cubeCandidates;
        /// ShadowRun::key per caster, flattened for the grouping pass.
        std::vector<uint32_t>              m_meshKeys;
        /// Slots a key's low digit counts: every mesh id, then every masked pair.
        uint32_t                           m_slotCount = 0;
        /// Per caster, the uploads its picture is drawn from; 0 for one not on the GPU.
        std::vector<uint64_t>              m_meshUploads;
        /// The alpha-masked casters, by position, while their keys are numbered.
        std::vector<uint32_t>              m_maskedCasters;
        std::vector<CullScratch>           m_scratch;         ///< One workspace per cull task.

        // The forked cull: the view it reads, the task body the pool borrows,
        // and the batch in flight until finishCull.
        const RenderView*                  m_cullView = nullptr;
        CullTaskBody                       m_cullTaskBody{this};
        std::optional<ThreadPool::Batch>   m_cullBatch;
};

} // namespace Vkm::Engine
