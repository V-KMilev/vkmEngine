#pragma once

#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "vkm_opengl/core/context.h"
#include "vkm_opengl/texture/texture.h"

#include "system/render/render_backend.h"
#include "system/render/render_view.h"
#include "core/frame/render_readiness.h"
#include "system/render/editor_render_hooks.h"

#include "hdr_image.h"

#include "gl_forward_programs.h"
#include "pass/gl_frame_context.h"
#include "storage/gl_target.h"
#include "asset/gl_asset_mirror.h"
#include "storage/light/gl_atmosphere.h"
#include "storage/gl_bloom.h"
#include "frame/gl_camera.h"
#include "frame/gl_frame_reader.h"
#include "frame/water/gl_waves.h"
#include "frame/water/gl_water_bodies.h"
#include "storage/light/gl_lightmaps.h"
#include "storage/water/gl_wave_fields.h"
#include "storage/water/gl_water_shore.h"
#include "storage/light/gl_cluster_grid.h"
#include "offline/capture/gl_cube_convolver.h"
#include "storage/light/gl_fog_volume.h"
#include "storage/light/gl_ibl.h"
#include "offline/gl_air_baker.h"
#include "offline/gl_ibl_baker.h"
#include "frame/draw/gl_instance_batcher.h"
#include "offline/gl_irradiance_baker.h"
#include "offline/gl_lightmap_baker.h"
#include "storage/gl_exposure.h"
#include "storage/light/gl_irradiance_volume.h"
#include "frame/light/gl_lights.h"
#include "asset/gl_mesh.h"
#include "frame/gl_object_buffer.h"
#include "frame/draw/gl_occlusion_cull.h"
#include "offline/gl_preview.h"
#include "offline/gl_probe_manager.h"
#include "offline/gl_baked_from.h"
#include "offline/gl_capture_lighting.h"
#include "offline/capture/gl_scene_capture.h"
#include "gl_screen_triangle.h"
#include "storage/light/gl_shadow_atlas.h"
#include "frame/light/gl_shadow_plan.h"
#include "frame/gl_skin_palette.h"

namespace Vkm::Platform {
class WindowSurface;
} // namespace Vkm::Platform

namespace Vkm::Engine {

class GLPass;
class GLCompositePass;
class GLSplashPass;
struct GLFrameContext;
struct Environment;

/**
 * @brief An ordered pass and the name its profiler zones carry.
 *
 * So the render loop names its CPU and GPU zones without a parallel name array.
 */
struct PassEntry {
    const char*             name;
    std::unique_ptr<GLPass> pass;
};

/**
 * @brief The OpenGL implementation of RenderBackend.
 *
 * Owns the GL state, the GPU mirror (GLAssetMirror) and an ordered list of passes, which do their own
 * clears. The buffer swap stays in the engine loop, after the editor UI draws, so this backend
 * draws but does not present.
 */
class GLBackend : public RenderBackend, public EditorRenderHooks {
    public:
        GLBackend();
        ~GLBackend() override;

        GLBackend(const GLBackend& other) = delete;
        GLBackend& operator=(const GLBackend& other) = delete;

        GLBackend(GLBackend && other) = delete;
        GLBackend& operator=(GLBackend && other) = delete;

    public:
        /**
         * @brief The surface this backend draws a window through, for setupEngineApp.
         *
         * @return A GL window surface: the context hints, the loader and the swap.
         */
        static std::unique_ptr<Platform::WindowSurface> windowSurface();

        /// This backend is its own editor hooks; see RenderBackend::editorHooks.
        EditorRenderHooks* editorHooks() override { return this; }

    public:
        bool init(Platform::WindowManager& window) override;
        void render(const RenderView& view, const ResourceManager& resources) override;

        // Editor previews: offscreen renders, cached per key (GLPreview).
        GpuTextureId renderPreview(const PreviewRequest& request, const ResourceManager& resources) override;
        uint64_t     previewLook() const override;
        GpuTextureId previewTexture(uint64_t key) const override;
        void releasePreview(uint64_t key) override;
        void releaseAllPreviews() override;

        GpuTextureId textureId(const TextureHandle& handle) const override;
        GpuTextureId ensureTexture(const TextureHandle& handle, const ResourceManager& resources) override;
        GpuTextureId chromeImage(const std::string& path) override;
        uint32_t reloadChangedShaders() override;
        bool reloadChangedGrading() override;

        uint32_t maxAnisotropy() const override;
        bool holdsPixels(const TextureHandle& texture, uint64_t version) const override;
        RenderReadiness readiness() const override;
        bool readFrame(const RenderView& view) override;
        bool takeFrame(FramePixels& frame, bool wait) override;

        /**
         * @brief The shadow plan, as the last frame that drew the world left it: for tests.
         *
         * @return The backend's.
         */
        const GLShadowPlan& shadowPlan() const { return m_shadowPlan; }

        /**
         * @brief Whether the world drawn last was drawn whole behind the loading screen first (warming,
         *        render()): for tests.
         *
         * @return True once every slice of it was drawn there.
         */
        bool warmedWhole() const;

    private:
        /**
         * @brief Puts shaderConstants() in front of every shader this backend builds.
         *
         * A member, not a call in init(): members below compile programs in their constructors,
         * which run before any body of this class. Declared first, as a shader built earlier gets
         * an empty prelude and fails on every constant it names.
         */
        struct ConstantsInstalled {
            ConstantsInstalled();
        };

        /**
         * @brief What partitionDrawables() saw of the water a frame draws.
         */
        struct DrawnWater {
            bool       shown = false;  ///< A drawn surface is water: its ripples are wanted.
            bool       swell = false;  ///< A drawn water takes waves: their swell is wanted.
            WaterShore shore;          ///< The water whose shore is captured, if any.
        };

    private:
        /**
         * @brief Drop every GPU cache the world it was built from has outlived.
         *
         * Caches skip GPU work by comparing what the scene supplies with what they built, which
         * repeats exactly when the source is replaced, so they would keep showing the previous
         * scene. GLAssetMirror mirrors assets; probe captures and the irradiance bake picture the world;
         * held shadow tiles picture both. A cache with its own staleness test is one this cannot
         * drop, so a new cache belongs here.
         *
         * @param view Carries the world epoch.
         * @param resources Carries the asset epoch.
         */
        void onWorldReplaced(const RenderView& view, const ResourceManager& resources);

        /**
         * @brief Whether every program built so far has compiled and been checked, latched once it
         *        has; checks those the driver is done with, for a share of the frame.
         *
         * Until then a frame draws the chrome alone (drawChrome()): a world pass would wait on its
         * program, holding the window. Programs built later, lazily, are waited on where used.
         *
         * @param quickOnly Whether to check only while checks are quick: what asks must not stall
         *                  (the splash, a preview). A check that proved slow then stops them.
         * @return True once all were ready.
         */
        bool programsReady(bool quickOnly);

        /**
         * @brief Draw a frame of the chrome alone - the splash and the loading screen, its water,
         *        its UI and its bar - over black: until the world's programs are compiled and
         *        checked, and while a loading screen covers the frame whole.
         *
         * @param view The frame's view, whose chrome is drawn.
         */
        void drawChrome(const RenderView& view);

        /**
         * @brief Draw the world through the passes: the frame's targets, shadow plan, buckets, blocks and
         *        batch, then every pass in order.
         *
         * @param view    The frame's view.
         * @param warming Whether it is a slice of a world drawn behind the loading screen (warmView()).
         * @param sunDir  Toward the sun.
         * @param sky     The procedural sky, if the scene's is.
         */
        void drawWorld(
            const RenderView& view,
            bool warming,
            const glm::vec3& sunDir,
            const std::optional<SkyParams>& sky
        );

        /**
         * @brief Upload what the frame's draws and its bakes index objects by: the bone palettes, every
         *        object's transform and its lightmap place.
         *
         * @param view  The frame's view.
         * @param boxed Whether the GPU occlusion cull reads every object's world box this frame.
         */
        void uploadObjects(const RenderView& view, bool boxed);

        /**
         * @brief The context the passes of a frame of @p view read.
         *
         * @param view        The frame's view.
         * @param sceneRender Where the scene draws: the multisampled target or the resolved one.
         * @return The context, its frame-varying parts left at their defaults.
         */
        GLFrameContext frameContext(const RenderView& view, GLTarget& sceneRender);

        /// Put the state every pass starts from: the default, with this frame's depth convention.
        void passState();

        /**
         * @brief Split the camera's objects into the opaque, alpha-mask and transparent buckets once.
         *
         * One material resolve each, shared by the passes. An object whose material has no GPU
         * copy counts as opaque.
         *
         * @param view   Whose objects are split.
         * @param listed Which of them: the camera's, or a warming frame's slice of the scene's.
         * @return Whether any is water, whether any takes waves, and the water whose shore is captured.
         */
        DrawnWater partitionDrawables(const RenderView& view, const std::vector<uint32_t>& listed);

        /**
         * @brief @p view with a particle and a strip of each blend before the eye, for a warming frame
         *        to draw the particle programs with.
         *
         * @param view The frame's.
         * @return The copy.
         */
        static RenderView warmView(const RenderView& view);

        /**
         * @brief At frame end, bake again the irradiance volume and the probes that changed, and
         *        take the next step of a relight or a probe refresh the lighting asked for.
         *
         * Only from a view that gathered the scene: a frame with no camera lists neither, and
         * taking that for a scene without them would drop and re-bake every capture.
         *
         * @param view Supplies the volume, the probes and the scene.
         */
        void bakeCaptures(const RenderView& view);

        /**
         * @brief Keep the IBL product set showing the equirectangular HDR the sky names.
         *
         * An image newly named is decoded on a worker, never in the frame, and the sky keeps the light
         * it had until the image lands; then it is baked at once. Only the newest named is baked. One
         * that will not decode is said, and the sky keeps its light.
         *
         * @param path The HDR the scene names.
         */
        void followHDRSky(const std::string& path);

        /// Drop the HDR asked for, decoding or baked, for a sky that names none.
        void forgetHDRSky();

        /**
         * @brief Keep the IBL product set showing the procedural atmosphere.
         *
         * A sky whose sun and moon have only drifted since the last frame (SkyParams::driftsFrom)
         * is baked a step a frame - more while it runs ahead of the sky shown, so a fast sun's
         * light keeps pace - and swapped in once complete, the next bake starting from where
         * the sun is then, so a moving sun costs no frame the whole bake. Anything else -
         * the first sky, a changed value, a jump - is baked at once, so nothing captured from
         * the IBL meanwhile shows a sky the scene has left.
         *
         * @param sky The atmosphere and the sun and moon it is lit by.
         */
        void followProceduralSky(const SkyParams& sky);

        /**
         * @brief Drop the baked environment, for a scene that names none.
         *
         * Ambient and skybox then draw as with no sky; a bake runs again once a scene asks for one.
         */
        void clearEnvironment();

        /**
         * @brief The procedural sky the environment describes.
         *
         * @param env The scene's environment.
         * @param sunDir Direction to the sun, derived from its angles.
         * @return What baking its procedural sky takes.
         */
        static SkyParams skyParams(const Environment& env, const glm::vec3& sunDir);

    private:
        ConstantsInstalled m_constants;  ///< First: everything below may compile a shader.

        Vkm::OpenGL::Context m_context;
        GLAssetMirror        m_mirror;

        // The asset and world identities the GPU caches were built against. See onWorldReplaced.
        uint64_t m_assetEpoch = 0;
        uint64_t m_worldEpoch = 0;

        // Batches the opaque bucket once per frame (see GLFrameContext::opaqueBatch).
        GLInstanceBatcher m_opaqueBatcher;
        GLOcclusionCull   m_occlusion;  ///< Culls m_opaqueBatcher on the GPU, when the settings ask.

        GLScreenTriangle m_screenTri;  ///< Shared fullscreen triangle, referenced by the frame context.
        GLMesh           m_unitCube;   ///< The one unit cube, lent to whatever draws a box.

        GLTarget m_sceneHDR{GLTarget::Layout::Scene};  ///< Single-sample resolved scene; at 1x MSAA the
                                                       ///< geometry passes draw straight into it.
        GLTarget m_sceneMS{GLTarget::Layout::Scene};   ///< Multisample scene while MSAA is on; resolved into
                                                       ///< m_sceneHDR.

        GLTarget m_postA{GLTarget::Layout::Color};         ///< Post scratch (ping).
        GLTarget m_postB{GLTarget::Layout::Color};         ///< Post scratch (pong).
        GLTarget m_ao{GLTarget::Layout::Color, GL_RGBA8};  ///< GTAO factor + octahedral bent normal, each in
                                                           ///< 0..1: eight bits apiece suffice.

        // Images the host's chrome asked for, by path: uploaded once, untouched by a project open.
        std::unordered_map<std::string, std::unique_ptr<Vkm::OpenGL::Texture2D>> m_chromeImages;

        GLCamera      m_camera;
        GLWaves       m_waves;
        GLWaterBodies m_waterBodies;  ///< Each Water's shares, while water is drawn.
        GLLights      m_lights;

        GLShadowAtlas m_shadowAtlas;
        GLShadowPlan  m_shadowPlan;
        bool          m_layoutRefusedLogged = false;  ///< Whether the atlas refusing the plan is reported.

        GLObjectBuffer m_objects;  ///< Every object's model matrix, once a frame.

        // Lent to the offline renderers below and the passes in m_passes: one compile of each
        // forward variant and of the convolution programs. Declared ahead of them, as they bind
        // references to them.
        GLForwardPrograms m_forwardPrograms;
        GLSceneCapture    m_sceneCapture;
        GLCubeConvolver   m_cubeConvolver;

        GLIBL         m_ibl;
        GLIBLBaker    m_iblBaker;     ///< Persistent: the procedural sky re-bakes whenever the sun moves.
        GLAirBaker    m_airBaker;     ///< The air's tables, baked when the air changes.
        GLAtmosphere  m_atmosphere;   ///< The procedural sky's tables.
        GLBloom       m_bloom;
        GLSkinPalette m_skinPalette;  ///< This frame's bone palettes, in one storage buffer.
        GLClusterGrid m_clusterGrid;  ///< Forward+ per-cluster light lists (compute-filled).
        GLFogVolume   m_fog;          ///< Froxel volumetric-fog volumes (compute-filled).

        GLWaveFields       m_waveFields;       ///< The swell and the ripple fields.
        GLWaterShore       m_waterShore;       ///< Water's shore, for its waterline foam.
        GLLightmaps        m_lightmaps;        ///< What fixed meshes read their fixed lights' shadows from.
        GLIrradianceVolume m_irradiance;       ///< Baked SH irradiance volume (GI).
        GLIrradianceBaker  m_irradianceBaker;  ///< Its bake, relight and their bookkeeping.
        GLLightmapBaker    m_lightmapBaker;    ///< Fills m_lightmaps when the fixed world changes.

        GLProbeManager m_probes;        ///< Reflection-probe arrays, baker, bake state, UBO.
        GLExposure     m_exposure;      ///< Auto-exposure's histogram and its readback.
        GLFrameReader  m_frameReader;   ///< The frames read back for screenshots.
        GLPreview      m_preview;       ///< Editor material/mesh preview renders.
        PreviewScene   m_previewScene;  ///< The last frame's light and tonemap, which a preview takes.

        // What the last frame went on to bake, of what readiness() counts.
        bool m_bakes     = false;    ///< Anything: it had a camera to gather from.
        bool m_bakesHeld = false;    ///< Nothing yet: its world was arriving (RenderView::worldArriving).

        bool m_eyeDrawn = false;  ///< The last frame drew the eye's view of this world
                                  ///< (GLFrameContext::continues).

        uint32_t              m_warmSlices = 0;      ///< This world's slices drawn under the loading screen.
        bool                  m_warmOwed   = false;  ///< A slice drawn last frame: readiness owes the rest.
        RenderView            m_warm;                ///< A warming frame's view, which its passes read.
        std::vector<uint32_t> m_warmList;            ///< A warming frame's slice of the scene's objects.

        // Per-frame draw buckets of object indices, refilled each frame with capacity kept.
        std::vector<uint32_t> m_opaque;
        std::vector<uint32_t> m_alphaMask;
        std::vector<uint32_t> m_transparent;

        std::vector<PassEntry> m_passes;
        GLCompositePass*       m_composite     = nullptr;  ///< In m_passes; its grading strip is reloaded.
        GLSplashPass*          m_splash        = nullptr;  ///< In m_passes; readiness() reports its images.
        std::vector<GLPass*>   m_chrome;                   ///< In m_passes: what drawChrome() runs.
        bool                   m_programsReady = false;    ///< programsReady(), latched.
        bool                   m_checksSlow    = false;    ///< A program's check took long (programsReady()).

        std::string           m_hdrPath;    ///< The HDR the sky last named: baked, decoding or unreadable;
                                            ///< empty while it names none.
        std::future<HDRImage> m_hdrDecode;  ///< m_hdrPath's image, decoding on a worker until it is baked.

        std::optional<SkyParams> m_shownSky;  ///< The procedural sky the IBL shows; none while it shows an
                                              ///< HDR or nothing.
        std::optional<SkyParams> m_askedSky;  ///< The procedural sky the last frame that had one asked for.

        BakedFrom<Atmosphere::Coefficients> m_bakedAir;
};

} // namespace Vkm::Engine
