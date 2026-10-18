#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "gl_context.h"
#include "gl_texture.h"

#include "system/render/render_backend.h"
#include "system/render/editor_render_hooks.h"

#include "gl_target.h"
#include "gl_view.h"
#include "storage/gl_atmosphere.h"
#include "storage/gl_bloom.h"
#include "frame/gl_camera.h"
#include "frame/gl_ocean.h"
#include "storage/gl_ocean_fields.h"
#include "storage/gl_cluster_grid.h"
#include "offline/gl_cube_convolver.h"
#include "storage/gl_fog_volume.h"
#include "storage/gl_ibl.h"
#include "offline/gl_ibl_baker.h"
#include "frame/gl_instance_batcher.h"
#include "offline/gl_irradiance_baker.h"
#include "storage/gl_exposure.h"
#include "storage/gl_irradiance_volume.h"
#include "frame/gl_lights.h"
#include "asset/gl_mesh.h"
#include "frame/gl_object_buffer.h"
#include "frame/gl_occlusion_cull.h"
#include "offline/gl_preview.h"
#include "storage/gl_probe_manager.h"
#include "offline/gl_baked_from.h"
#include "offline/gl_capture_lighting.h"
#include "offline/gl_scene_capture.h"
#include "gl_screen_triangle.h"
#include "storage/gl_shadow_atlas.h"
#include "frame/gl_shadow_data.h"
#include "frame/gl_skin_palette.h"

namespace Vkm::Engine {

class GLPass;
class GLPlanarReflectionPass;
class WindowSurface;
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
 * Owns the GL state, the GPU mirror (GLView) and an ordered list of passes, which do their own
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
        static std::unique_ptr<WindowSurface> windowSurface();

        /// This backend is its own editor hooks; see RenderBackend::editorHooks.
        EditorRenderHooks* editorHooks() override { return this; }

    public:
        bool init(WindowManager& window) override;
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

        // The editor's ImGui, drawn by GL into the window after the scene.
        void initImGui(GLFWwindow* window) override;
        void beginImGuiFrame() override;
        void renderImGui(ImDrawData* data) override;
        void shutdownImGui() override;

        uint32_t reloadChangedShaders() override;
        uint32_t maxAnisotropy() const override;
        bool holdsPixels(const TextureHandle& texture, uint64_t version) const override;
        bool readFrame(const RenderView& view, std::vector<uint8_t>& pixels) override;

        /**
         * @brief The GPU occlusion cull, as its last frame left it: for tests.
         *
         * @return The backend's cull of the opaque batch.
         */
        const GLOcclusionCull& occlusionCull() const { return m_occlusion; }

        /**
         * @brief The engine constants every shader stage is compiled with.
         *
         * GLSL cannot see a C++ header, so this writes the constants themselves as GLSL
         * (`Config::MAX_LIGHTS`, the probe capacity, IBL mip counts, `MODE_*` ordinals,
         * gl_bindings.h's points and units), and ConstantsInstalled hands the text to the shader
         * loader as a prelude. Public and static so the GPU test suite compiles the shipped
         * shaders exactly as the backend does, without a window.
         *
         * @return GLSL declarations, ready to sit under the `#version` line.
         */
        static std::string shaderConstants();

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

    private:
        /**
         * @brief Drop every GPU cache the world it was built from has outlived.
         *
         * Caches skip GPU work by comparing what the scene supplies with what they built, which
         * repeats exactly when the source is replaced, so they would keep showing the previous
         * scene. GLView mirrors assets; probe captures and the irradiance bake picture the world;
         * held shadow tiles picture both. A cache with its own staleness test is one this cannot
         * drop, so a new cache belongs here.
         *
         * @param view Carries the world epoch.
         * @param resources Carries the asset epoch.
         */
        void onWorldReplaced(const RenderView& view, const ResourceManager& resources);

        /**
         * @brief Split the camera's objects into the opaque, alpha-mask and transparent buckets once.
         *
         * One material resolve each, shared by the passes. An object whose material has no GPU
         * copy counts as opaque.
         *
         * @param view Whose visible objects are split.
         */
        void partitionDrawables(const RenderView& view);

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
         * @brief Bake the IBL product set again, from an equirectangular HDR.
         *
         * @param path The HDR the scene names.
         */
        void bakeEnvironment(const std::string& path);

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

        Vkm::GL::Context m_context;
        GLView           m_view;

        // The asset and world identities the GPU caches were built against. See onWorldReplaced.
        uint64_t m_assetEpoch = 0;
        uint64_t m_worldEpoch = 0;

        // Batches the opaque bucket once per frame (see GLFrameContext::opaqueBatch).
        GLInstanceBatcher m_opaqueBatcher;
        GLOcclusionCull   m_occlusion;  ///< Culls m_opaqueBatcher on the GPU, when the settings ask.

        ScreenTriangle m_screenTri;  ///< Shared fullscreen triangle, referenced by the frame context.
        GLMesh         m_unitCube;   ///< The one unit cube, lent to whatever draws a box.

        /// Single-sample resolved scene; at 1x MSAA the geometry passes draw straight into it.
        GLTarget m_sceneHDR{GLTarget::Layout::Scene};
        /// Multisample scene while MSAA is on; resolved into m_sceneHDR.
        GLTarget m_sceneMS{GLTarget::Layout::Scene};

        GLTarget m_postA{GLTarget::Layout::Color};  ///< Post scratch (ping).
        GLTarget m_postB{GLTarget::Layout::Color};  ///< Post scratch (pong).
        /// GTAO factor + octahedral bent normal, each in 0..1: eight bits apiece suffice.
        GLTarget m_ao{GLTarget::Layout::Color, GL_RGBA8};

        // Images the host's chrome asked for, by path: uploaded once, untouched by a project open.
        std::unordered_map<std::string, std::unique_ptr<Vkm::GL::Texture2D>> m_chromeImages;

        GLCamera m_camera;
        GLOcean  m_ocean;
        GLLights m_lights;

        GLShadowAtlas m_shadowAtlas;
        GLShadowData  m_shadowData;
        bool          m_layoutRefusedLogged = false;  ///< Whether the atlas refusing the plan is reported.

        /// Every object's model matrix, once a frame.
        GLObjectBuffer m_objects;

        // Lent to the offline renderers below and the forward pass in m_passes: one compile of
        // the PBR ubershader and the convolution programs. Declared ahead of them, as they bind
        // references to them.
        GLSceneCapture  m_sceneCapture;
        GLCubeConvolver m_cubeConvolver;

        GLIBL         m_ibl;
        GLIBLBaker    m_iblBaker;     ///< Persistent: the procedural sky re-bakes whenever the sun moves.
        GLAtmosphere  m_atmosphere;   ///< The procedural sky's tables.
        GLBloom       m_bloom;
        GLSkinPalette m_skinPalette;  ///< This frame's bone palettes, in one storage buffer.
        GLClusterGrid m_clusterGrid;  ///< Forward+ per-cluster light lists (compute-filled).
        GLFogVolume   m_fog;          ///< Froxel volumetric-fog volumes (compute-filled).

        GLOceanFields      m_oceanFields;  ///< The ocean's ripple fields and shore.
        GLIrradianceVolume m_irradiance;       ///< Baked SH irradiance volume (GI).
        GLIrradianceBaker  m_irradianceBaker;  ///< Its bake, relight and their bookkeeping.

        GLProbeManager m_probes;        ///< Reflection-probe arrays, baker, bake state, UBO.
        GLExposure     m_exposure;      ///< Auto-exposure's histogram and its readback.
        GLPreview      m_preview;       ///< Editor material/mesh preview renders.
        PreviewScene   m_previewScene;  ///< The last frame's light and grade, which a preview takes.

        // Per-frame draw buckets of object indices, refilled each frame with capacity kept.
        std::vector<uint32_t> m_opaque;
        std::vector<uint32_t> m_alphaMask;
        std::vector<uint32_t> m_transparent;

        std::vector<PassEntry> m_passes;
        GLPlanarReflectionPass* m_planar = nullptr;  ///< In m_passes; readied before they run.

        /// HDR path of the currently baked IBL; empty when none (or the sky is procedural).
        std::string m_bakedEnvPath;

        /// The procedural sky the IBL shows; none while it shows an HDR or nothing.
        std::optional<SkyParams> m_shownSky;
        /// The procedural sky the last frame that had one asked for.
        std::optional<SkyParams> m_askedSky;

        BakedFrom<Atmosphere::Coefficients> m_bakedAir;
};

} // namespace Vkm::Engine
