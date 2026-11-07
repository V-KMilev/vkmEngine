#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "frame/light/gl_shadow_block.h"
#include "system/render/render_limits.h"
#include "base/math/aabb.h"
#include "storage/light/gl_light_slots.h"
#include "vkm_platform/threading/thread_pool.h"
#include "storage/light/gl_shadow_atlas.h"

namespace Vkm::OpenGL {
    class UniformBuffer;
}

namespace Vkm::Engine {

class GLAssetMirror;
struct RenderView;
struct LightData;

/**
 * @brief A 2D depth render job: rasterize shadow casters into atlas tile `slot`.
 */
struct Shadow2DJob {
    glm::mat4 lightVP;
    uint32_t  slot;
    bool      cascade;                         ///< Whether this is one of the sun's movers' tiles rather than
                                               ///< a spot's map. It draws with depth clamped, so a caster
                                               ///< nearer the sun than its near plane is flattened onto it,
                                               ///< not clipped.
    glm::vec2 ditherOrigin = glm::vec2(0.0f);  ///< Where the tile's lower-left texel lies in the light's own
                                               ///< fixed grid, in texels: a cascade's from its snapped
                                               ///< center, zero for a spot's. Added to a fragment's place in
                                               ///< the tile it names one world texel however the cascade
                                               ///< snapped, which keys the transparent caster's dither so its
                                               ///< pattern stays put as the camera moves.
};

/**
 * @brief A rectangle of a held layer to draw again: what the eye's move uncovered, or all of it.
 */
struct ShadowHeldJob {
    glm::mat4      lightVP;  ///< The light's grid over `rect` alone, so what lies outside clips.
    ShadowHeldRect rect;
    uint32_t       layer;    ///< Its cascade.
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
    uint32_t first = 0;      ///< Where the run starts in ShadowCasterBatch::order.
    uint32_t count = 0;      ///< Casters in it.
    uint32_t key   = 0;      ///< What its casters share, `group * slots + slot`. The slot is the mesh id for
                             ///< solid casters; one cut by its alpha (GLMaterial::cutsItsShadow) takes a slot
                             ///< per (material, mesh) past every mesh id, so a run shares the material
                             ///< cutting its shadow. The group is the high digit (static, skinned as stored,
                             ///< skinned and posed), so runs come in that order and neither vertex layout nor
                             ///< pose alternates within a tile.
    bool     posed = false;  ///< Whether the frame posed its casters: the last group of `key`.
    bool     fixed = false;  ///< Whether its casters are fixed (ObjectDraw::fixed): the first group.
};

/**
 * @brief The casters one shadow job rasterizes: indices into RenderView::objects.
 *
 * Grouped by mesh (a caster cut by its alpha by mesh and material), then by whether posed, so the pass
 * issues one instanced draw per run the cull found.
 */
struct ShadowCasterBatch {
    std::vector<uint32_t>  order;
    std::vector<ShadowRun> runs;  ///< The groups of `order`, in order.

    uint64_t signature = 0;  ///< A hash of what the tile drawn from this batch looks like: the culling matrix
                             ///< and the tile's edge; the fixed survivors (ObjectDraw::fixed) by
                             ///< RenderObjects::fixedSerial and each fixed run's mesh id and uploads; each
                             ///< moving survivor by its mesh id, geometry upload (GLMesh::uploadId, and for
                             ///< one cut by its alpha its material's and albedo map's) and model. Equal
                             ///< signatures draw the same picture, so the atlas keeps the tile
                             ///< (GLShadowAtlas::tileHolds). Zero always draws: a posed survivor's picture is
                             ///< not in its model matrix.

    glm::mat4 culledFor{0.0f};  ///< The matrix it was culled against.
    uint32_t  culledEdge = 0;   ///< The edge of the tile or face it was culled for, in texels.
    uint64_t  culledAt   = 0;   ///< RenderObjects::serial of the frame it was culled or kept in.

    std::vector<uint32_t> fixedSurvivors;         ///< The fixed casters among the survivors, as caster
                                                  ///< positions, in order; while the matrix, the edge and
                                                  ///< RenderObjects::fixedSerial hold, a cull tests only what
                                                  ///< moves and keeps these.
    uint64_t              culledFixedSerial = 0;  ///< RenderObjects::fixedSerial they were culled at.
};

/**
 * @brief What a caster's shadow key is made from (GLShadowPlan::cullCasters): its object, mesh,
 *        material, and whether it is fixed and posed.
 *
 * Its matrix and box are not.
 */
struct KeyedCaster {
    uint32_t object   = 0;
    uint32_t mesh     = 0;
    uint32_t material = 0;
    bool     fixed    = false;
    bool     posed    = false;

    bool operator==(const KeyedCaster& other) const {
        return object == other.object && mesh == other.mesh && material == other.material
            && fixed == other.fixed && posed == other.posed;
    }
};

/**
 * @brief Per-task workspace for the cull, owned by GLShadowPlan and reused.
 *
 * One per job so tasks never share; kept between frames so a steady plan allocates nothing.
 */
struct CullScratch {
    std::vector<uint32_t> survivors;  ///< Frustum survivors, as caster positions, before mesh grouping.
    std::vector<uint32_t> counts;     ///< Per-key histogram, then the scatter cursor.
    std::vector<uint32_t> firstOf;    ///< Per fixed key, its first survivor, as a caster position.
};

/**
 * @brief Builds the frame's shadow plan and owns the ShadowBlock UBO.
 *
 * Culling lives here, not in the pass, because it is not a GL concern and the caster set is
 * scene-wide: a task per tile and face runs on the thread pool, behind a serial pass rejecting
 * most of the scene once against a point light's sphere instead of six times against its faces.
 */
class GLShadowPlan {
    public:
        GLShadowPlan();
        ~GLShadowPlan();

        GLShadowPlan(const GLShadowPlan& other) = delete;
        GLShadowPlan& operator=(const GLShadowPlan& other) = delete;

        GLShadowPlan(GLShadowPlan && other) = delete;
        GLShadowPlan& operator=(GLShadowPlan && other) = delete;

    public:
        /**
         * @brief Plan the frame's shadows: assign atlas slots, fit the matrices, record the jobs.
         *
         * Call before GLLights::update (which reads lightSlots()) and uploadAndBind, and only for a
         * frame whose shadow pass runs: its jobs are taken as drawn. The caster cull it forks is left
         * running; the batches are unreadable until finishCull, which the shadow pass calls: the cull
         * reads @p view's objects, rewritten for the next frame.
         *
         * Atlas slots run out before lights do (Config::NUM_CASCADES of the
         * Config::MAX_SHADOW_CASTERS_2D tiles go to the sun). The sun goes first whatever its
         * priority: spots ahead of it could leave too short a run for its consecutive cascades.
         * Then lights go by Light::shadowPriority, highest first, and - for a kind of tile more ask
         * for than there are - nearness to the eye; ties by entity slot, since `view.lists.lights` is
         * SparseSet packing order and destroying an unrelated light would hand a shadow to another
         * mid-session (findLowestSlot, ecs/scene.h).
         *
         * Each tile is sized as the plan fits it: what moves under the sun at @p tileRes, and a
         * spot by how much of the screen its cone covers. placeTiles() then takes where the atlas
         * put them.
         *
         * @param view    Its lights and its camera.
         * @param mirror  Already synced: a caster's geometry upload is part of its tile's look.
         * @param tileRes The largest tile's edge as the atlas built it
         *                (GLShadowAtlas::tileResolution), not as asked: a shadow texel's size is
         *                measured from the tiles.
         */
        void build(const RenderView& view, const GLAssetMirror& mirror, uint32_t tileRes);

        /**
         * @brief Forget what the held layers show, so each is drawn again, this frame.
         *
         * For a replaced world or asset graph, or a reloaded program.
         */
        void forgetHeld();

        /**
         * @brief Per 2D slot, the edge of its tile in texels, as build() sized it.
         *
         * @return What GLShadowAtlas::layout places; valid until the next build().
         */
        const std::vector<uint32_t>& tileSizes() const { return m_tileSizes; }

        /**
         * @brief Point each 2D slot's GPU entry at its tile, once the atlas has laid them out.
         *
         * @param atlas The atlas, laid out from tileSizes().
         */
        void placeTiles(const GLShadowAtlas& atlas);

        /**
         * @brief Upload the ShadowBlock UBO and bind it to its binding point.
         */
        void uploadAndBind();

        /**
         * @brief Join the caster cull that build forked.
         *
         * Returns at once when nothing is in flight. Call before reading batch2D or batchCube,
         * after the work the frame can do meanwhile, and before the frame ends either way.
         */
        void finishCull();

        /**
         * @brief The light the cascades were fitted for this frame, -1 for none.
         *
         * The first directional caster build() walks, by entity slot, unless the budget refused
         * it. Anything applied to the cascades' light asks here rather than picking a light by a
         * rule of its own.
         *
         * @return An index into `RenderView::SceneLists::lights`; valid until the next build().
         */
        int sunLight() const { return m_sunLight; }

        /**
         * @brief The packed shadow slot of each light this frame, -1 for none.
         *
         * Parallel to `RenderView::SceneLists::lights` as far as it goes; a light past the end has none.
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
         * @brief The rectangles of the held layers to draw again this frame.
         *
         * @return The jobs; valid until the next build().
         */
        const std::vector<ShadowHeldJob>& jobsHeld() const { return m_jobsHeld; }

        /**
         * @brief Fixed casters surviving the cull for held job @p jobIndex.
         *
         * @param jobIndex Index into jobsHeld().
         * @return The batch: its object indices grouped by key, and a run per key.
         */
        const ShadowCasterBatch& batchHeld(size_t jobIndex) const { return m_batchesHeld[jobIndex]; }

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
            GLShadowPlan* self;

            void operator()(size_t task) const { self->cullTask(task); }
        };

        /**
         * @brief A spot's tile edge, by the spot's entity slot.
         */
        struct SpotTile {
            uint32_t entitySlot = 0;
            uint32_t size       = 0;
        };

        /**
         * @brief What one held layer shows: which sun, which fixed casters, which part of the grid.
         *
         * The grid is the light's own, a texel of the layer's size per step, fixed in the world; a
         * texel of the layer is the place in the grid its coordinates wrap to, so the layer's window
         * moves over the grid without what it already shows moving in the layer.
         */
        struct HeldLayer {
            glm::vec3  dir         = glm::vec3(0.0f);  ///< The sun it was fitted to.
            float      texel       = 0.0f;
            float      nearDepth   = 0.0f;             ///< Along the sun: where the layer's depth starts.
            float      farDepth    = 0.0f;             ///< And where it ends.
            glm::ivec2 window      = glm::ivec2(0);    ///< The grid's texel at its lower-left corner.
            uint64_t   fixedSerial = 0;                ///< RenderObjects::fixedSerial it was drawn at.
            uint64_t   revision    = 0;                ///< GLAssetMirror::syncedRevision it was drawn under.
            bool       drawn       = false;

            /// The grid's texel at the corner of a window around @p eye, at @p step texels a move.
            glm::ivec2 windowAt(const glm::vec3& eye, uint32_t edge, uint32_t step) const;
        };

    private:
        /**
         * @brief Fork the cull of the caster list against every job recorded this frame.
         *
         * Each cascade, spot and cube face is an independent scan into its own batch, so nothing
         * synchronizes; a point light rejects against its sphere once and its faces refine that.
         * The tasks read only the view's objects, held still for the frame, and write only this
         * object's batches and scratch, so the main thread builds on until finishCull joins.
         *
         * @param view   Supplies the objects and the casters among them.
         * @param mirror For each caster's upload id.
         */
        void cullCasters(const RenderView& view, const GLAssetMirror& mirror);

        /**
         * @brief Cull one job's casters into its batch: a 2D tile, or one cube face.
         *
         * @param task The 2D jobs first, then six faces per cube job.
         */
        void cullTask(size_t task);

        /**
         * @brief The edge, in texels, of what cull task @p task draws into.
         *
         * @param task As cullTask's.
         * @return A 2D job's tile as build() sized it, or a cube's face.
         */
        uint32_t edgeOf(size_t task) const;

        /**
         * @brief Fit the sun's cascades: a held layer and a movers' tile each.
         *
         * Writes their GPU entries and jobs; a sun after the first takes none. A movers' tile carries
         * which filter it takes (params.w): the hard one for a sun with a size while soft shadows are
         * on, else one compare.
         *
         * @param view       Supplies the camera, and the fixed casters' reach along the sun.
         * @param mirror     Its upload revision is part of what a held layer shows.
         * @param light      The directional light.
         * @param lightIndex Its index in the frame's light list.
         * @param next2D     The next free 2D slot, advanced past the movers' tiles.
         */
        void fitSun(
            const RenderView& view,
            const GLAssetMirror& mirror,
            const LightData& light,
            uint32_t lightIndex,
            uint32_t& next2D
        );

        /**
         * @brief Say what cascade @p cascade's held layer draws again, then point its entry at it.
         *
         * @param cascade    The cascade.
         * @param wanted     The layer as it would be drawn now; its window is worked out here.
         * @param allChanged RenderObjects::allChanged: what the fixed casters are may have changed.
         */
        void fitHeld(uint32_t cascade, const HeldLayer& wanted, bool allChanged);

        /**
         * @brief Fit what moves under the sun in a cascade into the next free 2D slot.
         *
         * @param light     The sun.
         * @param halfWidth Half the width it covers, in meters.
         * @param next2D    The next free 2D slot, advanced past the one taken.
         */
        void fitMovers(const LightData& light, float halfWidth, uint32_t& next2D);

        /**
         * @brief Record the jobs that draw a rectangle of the grid into a held layer.
         *
         * A job for each part of the rectangle that does not wrap.
         *
         * @param cascade The layer.
         * @param from    The grid's texel at the rectangle's lower left.
         * @param to      One past its upper right.
         */
        void addHeldRect(uint32_t cascade, const glm::ivec2& from, const glm::ivec2& to);

        /**
         * @brief Fit a spot light into the next free 2D atlas slot.
         *
         * @param light      The spot light.
         * @param lightIndex Its index in the frame's light list.
         * @param next2D     The next free 2D slot, advanced past the one taken.
         */
        void fitSpot(const LightData& light, uint32_t lightIndex, uint32_t& next2D);

        /**
         * @brief A spot's tile edge: about SPOT_TEXELS_PER_PIXEL texels per pixel its cone covers.
         *
         * A power of two from SHADOW_ATLAS_MIN_TILE_RES to the largest tile. It shrinks only once
         * its cone needs well under the tile it had, so one at the edge of a size does not flip
         * between two every frame.
         *
         * @param light The spot light.
         * @param range Its range, as the fit clamped it.
         * @return The edge in texels.
         */
        uint32_t spotTileSize(const LightData& light, float range) const;

        /**
         * @brief Fit a point light into the next free shadow cube.
         *
         * @param light      The point light.
         * @param lightIndex Its index in the frame's light list.
         * @param nextCube   The next free cube, advanced past the one taken.
         */
        void fitPoint(const LightData& light, uint32_t lightIndex, uint32_t& nextCube);

    private:
        std::unique_ptr<Vkm::OpenGL::UniformBuffer> m_ubo;
        ShadowUBOData                               m_data{};
        ShadowUBOData                               m_last{};

        int      m_lightSlot[Config::MAX_LIGHTS];  ///< Per light that made the GPU list, its slot.
        uint32_t m_lightCount = 0;
        int      m_sunLight   = -1;                ///< See sunLight().

        uint32_t m_refused      = 0;      ///< Shadow casters this frame that found no free atlas tile.
        bool     m_budgetLogged = false;  ///< Whether that refusal has been reported; cleared once it stops.

        uint32_t m_tileRes = 0;  ///< The atlas's largest tile this frame, which the cascades and spot sizes
                                 ///< start from.

        std::vector<uint32_t> m_tileSizes;  ///< Per 2D slot, its tile's edge in texels.

        std::vector<SpotTile> m_spotTiles;      ///< Per spot, its tile's edge last frame: what a spot shrinks
                                                ///< from. Few, so searched in turn.
        std::vector<SpotTile> m_spotTilesNext;  ///< This frame's, swapped in.

        // The camera, for how much of the screen a spot covers.
        glm::vec3 m_eye          = glm::vec3(0.0f);
        float     m_focalPixels  = 1.0f;  ///< projection[1][1] times half the scene's height.
        bool      m_orthographic = false;

        bool m_soft = true;  ///< RenderSettings::softShadows: a sun with a size filters its movers' tile.

        HeldLayer m_held[Config::NUM_CASCADES];  ///< Each cascade's held layer, kept while the view stays
                                                 ///< inside its window.
        uint32_t  m_heldTurn = 0;                ///< The layer whose turn it is to catch up with the sun or
                                                 ///< the casters.

        Math::AABB m_fixedBox;                ///< Around every fixed caster.
        bool       m_anyFixed       = false;  ///< Whether there is one.
        uint64_t   m_fixedBoxSerial = 0;      ///< RenderObjects::fixedSerial it was worked out at.

        std::vector<uint32_t> m_shadowOrder;  ///< This frame's shadow-casting lights, in the order they take
                                              ///< tiles. A member for its capacity, like the cull scratch
                                              ///< below: refilled every frame.

        std::vector<Shadow2DJob>   m_jobs2D;
        std::vector<ShadowCubeJob> m_jobsCube;
        std::vector<ShadowHeldJob> m_jobsHeld;

        // Cull results, index-aligned with the jobs (cube batches six per job). Only ever cleared,
        // so a steady plan stops allocating after the first frame.
        std::vector<ShadowCasterBatch> m_batches2D;
        std::vector<ShadowCasterBatch> m_batchesCube;
        std::vector<ShadowCasterBatch> m_batchesHeld;

        // Scratch for the cull, kept alive between frames for the same reason.
        std::vector<std::vector<uint32_t>> m_cubeCandidates;        ///< Per point light, the positions of the
                                                                    ///< casters its sphere reaches.
        std::vector<uint32_t>              m_meshKeys;              ///< ShadowRun::key per caster, flattened
                                                                    ///< for the grouping pass.
        std::vector<uint32_t>              m_movingCasters;         ///< The positions of the casters that are
                                                                    ///< not fixed: all a cull keeping its
                                                                    ///< fixed ones tests.
        std::vector<uint32_t>              m_fixedCasters;          ///< The positions of the fixed casters:
                                                                    ///< what a held layer draws.
        uint32_t                           m_slotCount        = 0;  ///< Slots a key's low digit counts: every
                                                                    ///< mesh id, then every masked pair.
        std::vector<uint64_t>              m_meshUploads;           ///< Per caster, the uploads its picture
                                                                    ///< is drawn from; 0 for one not on the
                                                                    ///< GPU.
        std::vector<KeyedCaster>           m_keyed;                 ///< Per caster, what its key was made
                                                                    ///< from: a caster drawn otherwise makes
                                                                    ///< the keys again.
        uint64_t                           m_preparedSerial   = 0;  ///< RenderObjects::serial the keys were
                                                                    ///< made for.
        uint64_t                           m_preparedRevision = 0;  ///< GLAssetMirror::syncedRevision() the
                                                                    ///< keys were made under.
        std::vector<uint32_t>              m_meshGroups;            ///< Per caster, the key group of its
                                                                    ///< layout and pose (the key's high
                                                                    ///< digit).
        std::vector<uint32_t>              m_maskedCasters;         ///< The alpha-masked casters, by
                                                                    ///< position, while their keys are
                                                                    ///< numbered.
        std::vector<CullScratch>           m_scratch;               ///< One workspace per cull task.
        std::vector<uint8_t>               m_recull;                ///< Per cull task: whether its batch is
                                                                    ///< culled again this frame, or kept from
                                                                    ///< the last.
        std::vector<uint8_t>               m_movingOnly;            ///< Per 2D job culled again: whether only
                                                                    ///< its moving casters are
                                                                    ///< (fixedSurvivors hold).

        // The forked cull: the view it reads, the task body the pool borrows,
        // and the batch the pool runs it as, waited for by finishCull.
        const RenderView*                  m_cullView = nullptr;
        CullTaskBody                       m_cullTaskBody{this};
        Platform::ThreadPool::Batch        m_cullBatch{m_cullTaskBody};
};

} // namespace Vkm::Engine
