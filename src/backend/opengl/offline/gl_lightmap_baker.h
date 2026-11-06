#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "vkm_opengl/buffer/readback_buffer.h"
#include "vkm_opengl/buffer/shader_storage_buffer.h"
#include "vkm_opengl/shader/compute_shader.h"
#include "vkm_opengl/shader/shader.h"
#include "vkm_opengl/target/frame_buffer.h"
#include "vkm_opengl/texture/texture.h"
#include "vkm_opengl/texture/texture_2d_array.h"

#include "base/math/aabb.h"
#include "frame/draw/gl_instance_batcher.h"
#include "offline/gl_capture_settle.h"
#include "offline/capture/gl_caster_depth.h"
#include "resource/asset/mesh_asset.h"
#include "storage/light/gl_lightmaps.h"
#include "system/render/data/light_data.h"
#include "core/frame/render_readiness.h"

namespace Vkm::OpenGL {
    class Context;
}

namespace Vkm::Engine {

class GLObjectBuffer;
class GLAssetMirror;
struct RenderObjects;
struct RenderView;

/**
 * @brief Bakes the shadows fixed meshes cast in each fixed light's light (Light::movable off), and
 *        the share of the sky they leave each fixed surface, into the lightmaps of the fixed meshes
 *        they fall on (GLLightmaps).
 *
 * The receivers are the fixed meshes (Mesh::movable off), terrain chunks and terrain plants whose
 * mesh carries lightmap coordinates and whose material is Opaque. Receivers and casters alike are
 * drawn as the meshes they were given (RenderObjects::bases), not the levels of detail the eye
 * picked, so a bake is the same wherever the eye stands. Each receiver takes a square of a page, a
 * plant's every instance too, sized by its surface at TEXELS_PER_METER, inside a GUTTER. The fixed
 * lights GLLightmaps::bakedLights names each take a channel. A light draws its fixed casters into
 * maps of its own, the sun's tiled at SUN_TEXEL; each page's receivers are drawn there by their
 * lightmap coordinates into where each texel lies and faces, spread past the charts, and each texel
 * takes the share of the light's source it sees (shaders/opengl/offline/lightmap/shadow). The sky is baked as
 * SKY_DIRECTIONS suns over the dome, six maps to a view, each over every receiver at one texel:
 * each direction a texel faces and a caster hides takes its cosine's share off the sky it sees,
 * summed at half precision and written to the pages' eight bits after each pass. Each reflection
 * probe's center is taken the same way, facing up, a pass at a time: the share of the sky its
 * capture holds, read back once the bake ends (GLLightmaps::setProbeSky).
 *
 * Baked whenever the fixed meshes or fixed lights have changed and then held still
 * CAPTURE_SETTLE_FRAMES frames, as the probes and the irradiance volume bake: planned in one frame,
 * then taken a step a frame while the frame has a bake budget (RenderView::bakeBudget) - one view
 * of a light's maps over one page, the GPU's work - into a second set of pages the frames read only
 * once the bake ends (GLLightmaps::beginBake). While the game plays, out from under the loading
 * cover, a frame takes a piece of a step instead (piece()), so no frame takes much more than its
 * own, and makes at most one of the bake's targets: the sky then takes a view a direction at a
 * time, through one map. A change before then starts it over once settled.
 */
class GLLightmapBaker {
    public:
        /// How shaders/opengl/offline/lightmap/shadow tells a light's kinds and the sky apart
        /// (LIGHTMAP_KIND_* there).
        enum class LightKind : int {
            Sun,
            Spot,
            Point,
            Sky,
            Count
        };

        static constexpr float    TEXELS_PER_METER = 8.0f;  ///< Texels a meter of a receiver's surface takes,
                                                            ///< either way.
        static constexpr uint32_t MIN_SIDE         = MeshAsset::LIGHTMAP_PACK_TEXELS / 4;
            ///< The least side of a receiver's square: a quarter of the side its charts were packed for, so a
            ///< texel still lies between them.
        static constexpr uint32_t MAX_SIDE         = 1024;  ///< The largest side of a receiver's square.
        static constexpr uint32_t SUN_MAP          = 4096;  ///< The side of each of the sun's maps, tiled
                                                            ///< over the receivers.
        static constexpr float    SUN_TEXEL        = 0.5f / TEXELS_PER_METER;
            ///< A texel of the sun's maps, meters: half a receiver's.
        static constexpr uint32_t LOCAL_MAP        = 1024;  ///< A spot's map side, and each of a point
                                                            ///< light's six faces'.
        static constexpr int      DILATE_PASSES    = 4;     ///< Texels the receivers are spread past their
                                                            ///< charts.
        static constexpr uint32_t GUTTER           = 2;     ///< Texels a receiver's square keeps clear at its
                                                            ///< rim, which the spread fills.
        static_assert(GUTTER <= DILATE_PASSES, "the spread fills the gutter");
        static_assert(GUTTER >= (1u << (GLLightmaps::LEVELS - 1)), "a far level reads one square");
        static constexpr uint32_t REACH_TEXELS     = 64;    ///< The widest a texel's blocker search and
                                                            ///< filter reach, in map texels: how far the
                                                            ///< sun's tiles overlap.
        static constexpr uint32_t SKY_DIRECTIONS   = 96;    ///< The directions the sky is baked from, spread
                                                            ///< evenly over the dome, six to a view.
        static_assert(SKY_DIRECTIONS % 6 == 0, "a sky view draws six maps");
        static constexpr uint32_t SKY_MAP          = 4096;  ///< The side of each of the sky's maps, one over
                                                            ///< every receiver.
        static constexpr uint32_t PLAY_ROWS        = 512;   ///< While the game plays, the rows of a page a
                                                            ///< frame's piece bakes, and the casters it draws
                                                            ///< into a map: each piece a small share of a
                                                            ///< frame. Under the loading cover a step is
                                                            ///< whole.
        static constexpr uint32_t PLAY_CASTERS     = 512;

    public:
        GLLightmapBaker();
        ~GLLightmapBaker();

        GLLightmapBaker(const GLLightmapBaker& other) = delete;
        GLLightmapBaker& operator=(const GLLightmapBaker& other) = delete;

        GLLightmapBaker(GLLightmapBaker && other) = delete;
        GLLightmapBaker& operator=(GLLightmapBaker && other) = delete;

    public:
        /**
         * @brief Bake @p lightmaps again when what they were baked from has changed and settled.
         *
         * @param gl        The context the bake draws through.
         * @param view      The frame: its objects, their draws and its lights.
         * @param mirror    GPU mirror: meshes and materials.
         * @param objects   The frame's object buffer, bound for the draws.
         * @param lightmaps What the bake fills.
         */
        void update(
            Vkm::OpenGL::Context& gl,
            const RenderView& view,
            const GLAssetMirror& mirror,
            const GLObjectBuffer& objects,
            GLLightmaps& lightmaps
        );

        /// Whether the fixed world as last seen is still to be baked - settling, under way, or never -
        /// or a light that has held still since it moved is owed its channel.
        bool pending() const { return m_baking || m_baked != m_settle.last() || m_owed; }

        /**
         * @brief How far the bake owed has come, in steps.
         *
         * @return The steps of the bake under way; one step owed while one is settling or none was
         *         ever made; none when the lightmaps hold the world as it is.
         */
        RenderReadiness progress() const;

        /**
         * @brief Drop a bake under way and what it drew with, and bake again once the next world
         *        has settled, whatever it is made from.
         */
        void invalidate();

    private:
        /**
         * @brief One drawing of a light's maps, and the casters drawn, by key (RenderObjects::keys).
         */
        struct MapView {
            std::array<glm::mat4, 6> viewProj{};  ///< Each layer's, through GLDepth::shadowProjection.
            std::vector<uint64_t>    casters;
            float depth = 0.0f;  ///< The far plane; the sun's: the meters its depth spans.
            float rim   = 0.0f;  ///< The share of the map at each edge whose texels another tile bakes.
        };

        /**
         * @brief One baked light, as the bake planned it, or the sky, which a full bake takes last.
         */
        struct ChannelPlan {
            LightData            light;
            int                  channel    = -1;  ///< Its place among the lights baked; -1 the sky.
            LightKind            kind       = LightKind::Sun;
            int                  layers     = 1;   ///< Maps a view draws: six for a point light or the sky.
            uint32_t             side       = 0;   ///< Each map's side.
            float                source     = 0.0f;
            float                texelWorld = 0.0f;
            std::vector<MapView> views;
        };

    private:
        /// What a whole bake is made from: the fixed meshes, where they stand, and which fixed lights
        /// take which channel; not where the lights stand, which each light's channel answers alone.
        uint64_t askOf(const RenderView& view, const GLAssetMirror& mirror);

        /// Note where each light the bake takes stands, and which are owed their channel: held still
        /// CAPTURE_SETTLE_FRAMES frames as they stand, and not so in the shown set.
        void watchLights(const RenderView& view, const GLLightmaps& lightmaps);

        /// Whether the bake's @p place-th light has held still where it stands.
        bool stillLight(uint32_t place) const;

        /// The receivers' bound, and the fixed meshes that cast, which a bake's maps are drawn over.
        static void fixedExtent(
            const RenderView& view,
            const GLAssetMirror& mirror,
            Math::AABB& receivers,
            std::vector<uint32_t>& casters
        );

        /// Plan @p light's maps for channel @p channel, over @p receivers and through @p casters.
        static ChannelPlan planLight(
            const LightData& light,
            int channel,
            const RenderObjects& scene,
            const Math::AABB& receivers,
            const std::vector<uint32_t>& casters
        );

        /**
         * @brief Plan a bake of the owed lights' channels alone over a copy of the shown set: the fixed
         *        world is as the shown set was baked from, so its pages, places and sky stand.
         *
         * @param view      The frame: its objects and its lights.
         * @param mirror    GPU mirror: meshes and materials.
         * @param lightmaps Holds the shown set.
         */
        void planChannels(const RenderView& view, const GLAssetMirror& mirror, const GLLightmaps& lightmaps);

        /// Copy the shown set's next page and its sky into the bake's set: a step of planChannels' bake.
        void copyPage(GLLightmaps& lightmaps) const;

        /// The fixed meshes' part of askOf: where each stands and what it is drawn with.
        static uint64_t fixedDigest(const RenderObjects& objects, const GLAssetMirror& mirror);

        /**
         * @brief Plan a bake of @p view's fixed world into a new set of @p lightmaps: the
         *        receivers' places on the pages, each light's channel and the views of its maps.
         *
         * @param view      The frame: its objects and its lights.
         * @param mirror    GPU mirror: meshes and materials.
         * @param lightmaps What the bake fills: its new set begins here.
         * @return Whether there is anything to bake; with nothing, an empty set is shown at once.
         */
        bool plan(const RenderView& view, const GLAssetMirror& mirror, GLLightmaps& lightmaps);

        /**
         * @brief Take the bake's next step, or every step; the last shows the new set.
         *
         * @param gl        The context the bake draws through.
         * @param view      The frame: its objects and their draws.
         * @param mirror    GPU mirror: meshes and materials.
         * @param objects   The frame's object buffer, bound for the draws.
         * @param lightmaps What the bake fills.
         * @param sliced    One step, as a frame with a bake budget takes; false takes them all.
         */
        void slice(
            Vkm::OpenGL::Context& gl,
            const RenderView& view,
            const GLAssetMirror& mirror,
            const GLObjectBuffer& objects,
            GLLightmaps& lightmaps,
            bool sliced
        );

        /// Keep the index of @p scene's objects by key up to date with the cull.
        void index(const RenderObjects& scene);

        /// @p keys' objects' indices in this frame's objects, those still there.
        const std::vector<uint32_t>& resolve(const std::vector<uint64_t>& keys);

        /**
         * @brief Make the bake's own targets, one a call while @p small.
         *
         * @param small Whether a frame takes one piece, as while the game plays.
         * @return Whether all are made.
         */
        bool ensureScratch(bool small);

        /**
         * @brief Take the next piece of the step under way: a layer of its view's maps, its page's
         *        receivers or their spread, or its texels - while @p small, PLAY_CASTERS casters of a
         *        layer, one pass of the spread or PLAY_ROWS rows.
         *
         * @param gl        The context the bake draws through.
         * @param view      The frame: its objects and their draws.
         * @param mirror    GPU mirror: meshes and materials.
         * @param objects   The frame's object buffer, bound for the draws.
         * @param lightmaps What the bake fills.
         * @param small     Whether the piece is a frame's share while the game plays.
         * @return Whether the step is done.
         */
        bool piece(
            Vkm::OpenGL::Context& gl,
            const RenderView& view,
            const GLAssetMirror& mirror,
            const GLObjectBuffer& objects,
            GLLightmaps& lightmaps,
            bool small
        );

        /**
         * @brief Draw a page's receivers into where each texel lies and faces.
         *
         * @param gl        The context the bake draws through.
         * @param view      The frame: its objects and their draws.
         * @param mirror    GPU mirror: meshes and materials.
         * @param objects   The frame's object buffer, bound for the draws.
         * @param receivers The page's receivers, as indices in this frame's objects.
         */
        void rasterReceivers(
            Vkm::OpenGL::Context& gl,
            const RenderView& view,
            const GLAssetMirror& mirror,
            const GLObjectBuffer& objects,
            const std::vector<uint32_t>& receivers
        );

        /// Spread the page's receivers a texel past their charts: pass @p pass of DILATE_PASSES.
        void spread(int pass);

        /// Set the bake shader's maps and directions for the pass under way of @p planned's @p mapView.
        void passUniforms(const ChannelPlan& planned, const MapView& mapView);

        /// Size the light maps to @p side by @p layers, of depth @p format; whether they were made anew.
        bool ensureMaps(uint32_t side, int layers, GLenum format);

        /// Free the bake's large scratch targets, which only a bake uses, between bakes far apart.
        void releaseScratch();

        /// Begin planning a bake: its scratch freed, its clock started, its cursor at the first step.
        void restart();

    private:
        Vkm::OpenGL::Shader        m_raster;  ///< shaders/opengl/offline/lightmap/raster
        Vkm::OpenGL::ComputeShader m_dilate;  ///< shaders/opengl/offline/lightmap/dilate
        Vkm::OpenGL::ComputeShader m_shadow;  ///< shaders/opengl/offline/lightmap/shadow

        Vkm::OpenGL::FrameBuffer                     m_pageTarget;     ///< Draws into the position and normal
                                                                       ///< textures.
        Vkm::OpenGL::FrameBuffer                     m_mapTarget;      ///< Draws into a layer of the light
                                                                       ///< maps.
        std::unique_ptr<Vkm::OpenGL::Texture2D>      m_position[2];    ///< A page's texels' positions and
                                                                       ///< normals, ping-ponged; bake
                                                                       ///< scratch.
        std::unique_ptr<Vkm::OpenGL::Texture2D>      m_normal[2];
        std::unique_ptr<Vkm::OpenGL::Texture2DArray> m_maps;           ///< The light's maps, a layer a view.
        std::unique_ptr<Vkm::OpenGL::Texture2DArray> m_spentMaps;      ///< A light's before, freed m_spentIn
                                                                       ///< on.
        uint32_t                                     m_spentIn   = 0;
        uint32_t                                     m_releaseIn = 0;  ///< Frames until the targets a
                                                                       ///< finished bake drew with are freed.
        std::unique_ptr<Vkm::OpenGL::Texture2DArray> m_skyLeft;        ///< The sky each texel has left as the
                                                                       ///< sky's views take their shares,
                                                                       ///< R16F a page.

        GLInstanceBatcher m_batcher;  ///< The receivers, grouped.
        GLCasterDepth     m_casters;  ///< Draws the light's maps.

        // The bake under way: its plan, by key, and how far it has come.
        std::vector<ChannelPlan>           m_plan;
        std::vector<std::vector<uint64_t>> m_pageKeys;           ///< Each page's receivers.
        bool                               m_baking    = false;
        uint64_t                           m_bakingAsk = 0;      ///< The ask it bakes.
        uint32_t                           m_channel   = 0;      ///< The next step's light,
        uint32_t                           m_view      = 0;      ///< its view,
        uint32_t                           m_page      = 0;      ///< and page.
        uint32_t                           m_stepsDone = 0;
        uint32_t                           m_steps     = 0;
        int64_t                            m_mapsHold  = -1;     ///< The pass the maps hold: channel, view
                                                                 ///< and m_dir; -1 none.
        int64_t                            m_pageHold  = -1;     ///< The page the position and normal
                                                                 ///< textures hold; -1 none.
        int                                m_layer     = 0;      ///< The layer of the step's maps being
                                                                 ///< drawn,
        uint32_t                           m_casterAt  = 0;      ///< and its next caster.
        int                                m_spread    = -1;     ///< The page's spread passes done; -1 before
                                                                 ///< its receivers.
        uint32_t                           m_row       = 0;      ///< The step's next row of texels.
        int                                m_dir       = 0;      ///< The view's first map the pass under way
                                                                 ///< takes,
        int                                m_pass      = 0;      ///< and how many: its layers, or one of the
                                                                 ///< sky's while small.
        uint32_t                           m_receivers = 0;      ///< Receivers placed, for the log.
        uint32_t                           m_frames    = 0;      ///< Frames the bake has taken.
        double                             m_started   = 0.0;    ///< Seconds the planning began at, steady
                                                                 ///< clock.

        std::unordered_map<uint64_t, uint32_t> m_indexOf;              ///< Key -> this frame's object index.
        std::vector<uint64_t>                  m_keyOf;                ///< Index -> key, as m_indexOf was
                                                                       ///< built.
        uint64_t                               m_indexSerial = 0;      ///< RenderObjects::serial it was built
                                                                       ///< at.
        bool                                   m_indexed     = false;
        GLLightmaps::Places                    m_places;               ///< The plan's places, until the set
                                                                       ///< begins.
        std::vector<GLLightmaps::BakedLight>   m_channels;             ///< And its channels.
        bool                                   m_begun       = true;   ///< Whether the planned set has begun.
        std::vector<uint32_t>                  m_resolved;             ///< resolve()'s answer.
        std::vector<uint32_t>                  m_chunk;                ///< The casters a piece draws.

        // The reflection probes' centers the bake takes, the sky each has left as the passes take
        // their shares, and its read back, with the probes it is of.
        std::vector<uint32_t>                             m_probeSlots;
        std::vector<glm::vec3>                            m_probeAt;
        std::unique_ptr<Vkm::OpenGL::ShaderStorageBuffer> m_probeSky;
        std::unique_ptr<Vkm::OpenGL::ReadbackBuffer>      m_probeRead;
        std::vector<uint32_t>                             m_readSlots;
        std::vector<uint32_t>                             m_lights;      ///< GLLightmaps::bakedLights, for
                                                                         ///< askOf and plan.

        CaptureSettle<uint64_t> m_settle;
        std::optional<uint64_t> m_baked;  ///< The ask the lightmaps hold; none before the first bake.

        std::vector<std::pair<uint32_t, CaptureSettle<uint64_t>>> m_lightRest;
            ///< Each light the bake takes, in channel order: its entity slot, and its shape as it settles.
        std::vector<uint32_t> m_owedLights;    ///< Their places owed a channel, watchLights' answer.
        bool                  m_owed = false;  ///< Whether any is.

        bool                               m_partial = false;  ///< The bake under way re-bakes some channels
                                                               ///< over the shown set.
        uint32_t                           m_copied  = 0;      ///< Of a partial bake, the shown pages copied
                                                               ///< into its set.
        std::vector<std::vector<uint64_t>> m_shownPageKeys;    ///< The shown set's pages' receivers.

        uint64_t m_fixedDigest   = 0;      ///< fixedDigest, as of the two below.
        uint64_t m_fixedSerial   = 0;      ///< RenderObjects::fixedSerial it was worked out at.
        uint64_t m_fixedRevision = 0;      ///< GLAssetMirror::syncedRevision it was worked out at.
        bool     m_fixedKnown    = false;
};

} // namespace Vkm::Engine
