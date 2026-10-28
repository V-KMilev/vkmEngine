#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "gl_compute_shader.h"
#include "gl_frame_buffer.h"
#include "gl_shader.h"
#include "gl_texture.h"
#include "gl_texture_2d_array.h"

#include "frame/gl_instance_batcher.h"
#include "offline/gl_capture_settle.h"
#include "offline/gl_caster_depth.h"
#include "resource/asset/mesh_asset.h"
#include "storage/gl_lightmaps.h"
#include "system/render/data/light_data.h"
#include "system/render/render_backend.h"

namespace Vkm::GL {
    class Context;
}

namespace Vkm::Engine {

class GLObjectBuffer;
class GLView;
struct RenderObjects;
struct RenderView;

/**
 * @brief Bakes the shadows fixed meshes cast in each fixed light's light (Light::movable off) into
 *        the lightmaps of the fixed meshes they fall on (GLLightmaps).
 *
 * The receivers are the fixed meshes (Mesh::movable off), terrain chunks and terrain plants whose
 * mesh carries lightmap coordinates and whose material is Opaque. Receivers and casters alike are
 * drawn as the meshes they were given (RenderObjects::bases), not the levels of detail the eye
 * picked, so a bake is the same wherever the eye stands. Each receiver takes a square of a page, a
 * plant's every instance too, sized by its surface at TEXELS_PER_METRE, inside a GUTTER. The fixed
 * lights GLLightmaps::bakedLights names each take a channel. A light draws its fixed casters into
 * maps of its own, the sun's tiled at SUN_TEXEL; each page's receivers are drawn there by their
 * lightmap coordinates into where each texel lies and faces, spread past the charts, and each texel
 * takes the share of the light's source it sees (shaders/lightmap/shadow).
 *
 * Baked whenever the fixed meshes or fixed lights have changed and then held still
 * CAPTURE_SETTLE_FRAMES frames, as the probes and the irradiance volume bake: planned in one frame,
 * then taken a step a frame while the frame has a bake budget (RenderView::bakeBudget) - one view
 * of a light's maps over one page, the GPU's work - into a second set of pages the frames read only
 * once the bake ends (GLLightmaps::beginBake). A change before then starts it over once settled.
 */
class GLLightmapBaker {
    public:
        /// How shaders/lightmap/shadow tells a light's kinds apart (LIGHTMAP_KIND_* there).
        enum class LightKind : int {
            Sun,
            Spot,
            Point,
            Count
        };

        /// Texels a metre of a receiver's surface takes, either way.
        static constexpr float    TEXELS_PER_METRE = 8.0f;
        /// The least side of a receiver's square: a quarter of the side its charts were packed for,
        /// so a texel still lies between them.
        static constexpr uint32_t MIN_SIDE         = MeshAsset::LIGHTMAP_PACK_TEXELS / 4;
        /// The most.
        static constexpr uint32_t MAX_SIDE         = 1024;
        /// The side of each of the sun's maps, tiled over the receivers.
        static constexpr uint32_t SUN_MAP          = 4096;
        /// A texel of the sun's maps, metres: half a receiver's.
        static constexpr float    SUN_TEXEL        = 0.5f / TEXELS_PER_METRE;
        /// A spot's map side, and each of a point light's six faces'.
        static constexpr uint32_t LOCAL_MAP        = 1024;
        /// Texels the receivers are spread past their charts.
        static constexpr int      DILATE_PASSES    = 4;
        /// Texels a receiver's square keeps clear at its rim, which the spread fills.
        static constexpr uint32_t GUTTER           = 2;
        static_assert(GUTTER <= DILATE_PASSES, "the spread fills the gutter");
        static_assert(GUTTER >= (1u << (GLLightmaps::LEVELS - 1)), "a far level reads one square");
        /// The widest a texel's blocker search and filter reach, in map texels: how far the sun's
        /// tiles overlap.
        static constexpr uint32_t REACH_TEXELS     = 64;

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
         * @param glView    GPU mirror: meshes and materials.
         * @param objects   The frame's object buffer, bound for the draws.
         * @param lightmaps What the bake fills.
         */
        void update(
            Vkm::GL::Context& gl,
            const RenderView& view,
            const GLView& glView,
            const GLObjectBuffer& objects,
            GLLightmaps& lightmaps
        );

        /// Whether the fixed world as last seen is still to be baked: settling, under way, or never.
        bool pending() const { return m_baking || m_baked != m_settle.last(); }

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

        /// Whether the bake's own targets are held: only while a bake is under way.
        bool holdsScratch() const { return m_maps != nullptr || m_position[0] != nullptr; }

    private:
        /// One drawing of a light's maps, and the casters drawn, by key (RenderObjects::keys).
        struct MapView {
            std::array<glm::mat4, 6> viewProj{};  ///< Each layer's, through GLDepth::shadowProjection.
            std::vector<uint64_t>    casters;
            float depth = 0.0f;  ///< The far plane; the sun's: the metres its depth spans.
            float rim   = 0.0f;  ///< The share of the map at each edge whose texels another tile bakes.
        };

        /// One baked light, as the bake planned it: its channel is its place in the plan.
        struct ChannelPlan {
            LightData            light;
            LightKind            kind       = LightKind::Sun;
            int                  layers     = 1;  ///< Maps a view draws: six for a point light.
            uint32_t             side       = 0;  ///< Each map's side.
            float                source     = 0.0f;
            float                zNear      = 0.0f;
            float                texelWorld = 0.0f;
            std::vector<MapView> views;
        };

    private:
        /// What a bake is made from: the fixed meshes, where they stand, and the fixed lights.
        uint64_t askOf(const RenderView& view, const GLView& glView);

        /// The fixed meshes' part of askOf: where each stands and what it is drawn with.
        static uint64_t fixedDigest(const RenderObjects& objects, const GLView& glView);

        /**
         * @brief Plan a bake of @p view's fixed world into a new set of @p lightmaps: the
         *        receivers' places on the pages, each light's channel and the views of its maps.
         *
         * @param view      The frame: its objects and its lights.
         * @param glView    GPU mirror: meshes and materials.
         * @param lightmaps What the bake fills: its new set begins here.
         * @return Whether there is anything to bake; with nothing, an empty set is shown at once.
         */
        bool plan(const RenderView& view, const GLView& glView, GLLightmaps& lightmaps);

        /**
         * @brief Take the bake's next step, or every step; the last shows the new set.
         *
         * @param gl        The context the bake draws through.
         * @param view      The frame: its objects and their draws.
         * @param glView    GPU mirror: meshes and materials.
         * @param objects   The frame's object buffer, bound for the draws.
         * @param lightmaps What the bake fills.
         * @param sliced    One step, as a frame with a bake budget takes; false takes them all.
         */
        void slice(
            Vkm::GL::Context& gl,
            const RenderView& view,
            const GLView& glView,
            const GLObjectBuffer& objects,
            GLLightmaps& lightmaps,
            bool sliced
        );

        /// @p keys' objects' indices in this frame's objects, those still there.
        const std::vector<uint32_t>& resolve(const std::vector<uint64_t>& keys);

        /**
         * @brief Draw a page's receivers into where each texel lies and faces, and spread them past
         *        their charts.
         *
         * @param gl        The context the bake draws through.
         * @param view      The frame: its objects and their draws.
         * @param glView    GPU mirror: meshes and materials.
         * @param objects   The frame's object buffer, bound for the draws.
         * @param receivers The page's receivers, as indices in this frame's objects.
         */
        void drawReceivers(
            Vkm::GL::Context& gl,
            const RenderView& view,
            const GLView& glView,
            const GLObjectBuffer& objects,
            const std::vector<uint32_t>& receivers
        );

        /// Size the light maps to @p side by @p layers.
        void ensureMaps(uint32_t side, int layers);

        /// Free the bake's large scratch targets, which only a bake uses, between bakes far apart.
        void releaseScratch();

    private:
        Vkm::GL::Shader        m_raster;  ///< shaders/lightmap/raster
        Vkm::GL::ComputeShader m_dilate;  ///< shaders/lightmap/dilate
        Vkm::GL::ComputeShader m_shadow;  ///< shaders/lightmap/shadow

        Vkm::GL::FrameBuffer m_pageTarget;  ///< Draws into the position and normal textures.
        Vkm::GL::FrameBuffer m_mapTarget;   ///< Draws into a layer of the light maps.
        /// A page's texels' positions and normals, ping-ponged; bake scratch.
        std::unique_ptr<Vkm::GL::Texture2D>      m_position[2];
        std::unique_ptr<Vkm::GL::Texture2D>      m_normal[2];
        std::unique_ptr<Vkm::GL::Texture2DArray> m_maps;  ///< The light's maps, a layer a view.

        GLInstanceBatcher m_batcher;  ///< The receivers, grouped.
        GLCasterDepth     m_casters;  ///< Draws the light's maps.

        // The bake under way: its plan, by key, and how far it has come.
        std::vector<ChannelPlan>           m_plan;
        std::vector<std::vector<uint64_t>> m_pageKeys;  ///< Each page's receivers.
        bool     m_baking     = false;
        uint64_t m_bakingAsk  = 0;  ///< The ask it bakes.
        uint32_t m_channel    = 0;  ///< The next step's light,
        uint32_t m_view       = 0;  ///< its view,
        uint32_t m_page       = 0;  ///< and page.
        uint32_t m_stepsDone  = 0;
        uint32_t m_steps      = 0;
        int64_t  m_mapsHold   = -1;  ///< The step's view the maps hold, channel and view; -1 none.
        int64_t  m_pageHold   = -1;  ///< The page the position and normal textures hold; -1 none.
        uint32_t m_receivers  = 0;   ///< Receivers placed, for the log.
        uint32_t m_frames     = 0;   ///< Frames the bake has taken.
        double   m_started    = 0.0; ///< Seconds the planning began at, steady clock.

        std::unordered_map<uint64_t, uint32_t> m_indexOf;  ///< Key -> this frame's object index.
        std::vector<uint32_t>                  m_resolved; ///< resolve()'s answer.
        std::vector<uint32_t>                  m_lights;   ///< GLLightmaps::bakedLights, for askOf and plan.

        CaptureSettle<uint64_t> m_settle;
        std::optional<uint64_t> m_baked;  ///< The ask the lightmaps hold; none before the first bake.

        uint64_t m_fixedDigest   = 0;      ///< fixedDigest, as of the two below.
        uint64_t m_fixedSerial   = 0;      ///< RenderObjects::fixedSerial it was worked out at.
        uint64_t m_fixedRevision = 0;      ///< GLView::syncedRevision it was worked out at.
        bool     m_fixedKnown    = false;
};

} // namespace Vkm::Engine
