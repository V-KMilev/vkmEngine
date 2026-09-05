#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "gl_context.h"
#include "data/gl_screen_triangle.h"

#include "system/render/render_backend.h"
#include "system/render/editor_render_hooks.h"
#include "gl_view.h"
#include "data/gl_instance_batcher.h"
#include "gl_target.h"
#include "data/gl_camera.h"
#include "data/gl_lights.h"
#include "data/gl_shadow_atlas.h"
#include "data/gl_shadow_data.h"
#include "data/gl_cube_convolver.h"
#include "data/gl_scene_capture.h"
#include <string>
#include <unordered_map>

#include "gl_texture.h"

#include "data/gl_ibl.h"
#include "data/gl_ibl_baker.h"
#include "data/gl_bloom.h"
#include "data/gl_hiz.h"
#include "data/gl_skin_palette.h"
#include "data/gl_cluster_grid.h"
#include "data/gl_fog_volume.h"
#include "data/gl_irradiance_volume.h"
#include "data/gl_irradiance_baker.h"
#include "data/gl_probe_manager.h"
#include "data/gl_preview.h"

namespace Vkm::Engine {
    class GLPass;
    struct DrawableData;
    struct Environment;
}

namespace Vkm::Engine {
/**
 * @brief An ordered pass plus its profiler/debug label. The label rides with
 * the pass so the render loop names its CPU + GPU zones without a parallel name
 * array to keep in sync.
 */
struct PassEntry {
    const char*             name;
    std::unique_ptr<GLPass> pass;
};

/**
 * @brief The OpenGL implementation of RenderBackend.
 *
 * Owns the GL context state, the GPU resource mirror (GLView), and an ordered
 * list of passes; clearing is delegated to the passes. The window's buffer swap
 * stays in the engine loop (it must happen after the editor UI draws), so this
 * backend draws but does not present.
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
        /// This backend is its own editor hooks; see RenderBackend::editorHooks.
        EditorRenderHooks* editorHooks() override { return this; }

    public:
        bool init(WindowManager& window) override;
        void render(const RenderView& view, const ResourceManager& resources) override;

        // Editor previews: offscreen studio renders, cached per key (GLPreview).
        GpuTextureId renderPreview(const PreviewRequest& request,
                               const ResourceManager& resources) override;
        GpuTextureId previewTexture(uint64_t key) const override;
        void releasePreview(uint64_t key) override;
        void releaseAllPreviews() override;
        GpuTextureId textureId(const TextureHandle& handle) const override;
        GpuTextureId ensureTexture(const TextureHandle& handle,
                                   const ResourceManager& resources) override;
        GpuTextureId chromeImage(const std::string& path) override;
        uint32_t reloadChangedShaders() override;
        uint32_t maxAnisotropy() const override;

        /**
         * @brief The engine constants every shader stage is compiled with.
         *
         * GLSL cannot see a C++ header, so a cross-language constant is either
         * copied into each shader that needs it or written into GLSL by
         * something that knows both. This writes them out from the constants
         * themselves - `Config::MAX_LIGHTS`, the probe array's capacity, the
         * IBL mip counts, the `MODE_*` ordinals - and `init` hands the text to
         * the shader loader as a prelude, so a shader simply uses `MAX_LIGHTS`
         * and declares nothing.
         *
         * Public and static so the GPU test suite can compile the shipped
         * shaders exactly as the backend does, which is the only way to find
         * out that they still compile without opening a window.
         *
         * @return GLSL declarations, ready to sit under the `#version` line.
         */
        static std::string shaderConstants();

    private:
        /**
         * @brief Puts shaderConstants() in front of every shader this backend builds.
         *
         * A member rather than a call in init(), because several members below
         * build a program in their own constructor and member construction runs
         * before any function body of this class. Declared first, so the engine's
         * constants are installed before the first of them asks for a shader -
         * one built before that sees a prelude with nothing in it, and fails on
         * every constant the source names.
         */
        struct ConstantsInstalled {
            ConstantsInstalled();
        };

    private:
        /**
         * @brief Drop every GPU cache the world it was built from has outlived.
         *
         * Several caches skip redundant GPU work by remembering what they last
         * built and comparing it against what the scene supplies - handle and
         * version for GLView, position and bakeVersion for the probe array, box
         * and grid for the irradiance volume. Each of those repeats exactly when
         * what it was built from is replaced, so each would skip work it must
         * redo and go on showing the previous scene.
         *
         * The asset graph and the world are replaced separately, and each drops
         * what belongs to it: GLView mirrors assets, while the probe captures and
         * the irradiance bake are pictures of a place. The editor's play-stop
         * restore replaces the second alone, keeping the graph so the handles the
         * undo history holds still mean something.
         *
         * Detecting both here, once, is the point: a cache inventing its own
         * staleness test is how the probe array and the irradiance volume came to
         * be missed when GLView was fixed. A new cache belongs in this function.
         *
         * @param view The frame's view, carrying the world epoch.
         * @param resources The frame's resource manager, carrying the asset epoch.
         */
        void onWorldReplaced(const RenderView& view, const ResourceManager& resources);

        /**
         * @brief Split the frame's drawables into the opaque + transparent buckets once
         * (one material resolve each) so the depth prepass and forward pass share
         * the result instead of re-partitioning the list a pass apiece.
         *
         * A material that has not resolved yet counts as opaque here and in the
         * forward pass's own fallback, so the two never disagree about where a
         * drawable still streaming its material draws.
         */
        void partitionDrawables(const RenderView& view);

        /**
         * @brief (Re)bake the IBL product set (irradiance + prefilter + BRDF LUT) from
         * an equirectangular HDR. Drives both ambient lighting and the skybox.
         */
        void bakeEnvironment(const std::string& path);

        /**
         * @brief (Re)bake the IBL product set from the procedural atmosphere, with
         * the sun at @p sunDir (direction to the sun) and @p env's sky params.
         */
        void bakeProceduralSky(const Environment& env, const glm::vec3& sunDir);

        /**
         * @brief A cached bake, and the signature it was baked from.
         *
         * A bake is skipped when nothing it depends on changed, and the way that
         * goes wrong is always the same: a field joins the signature and is
         * assigned where the bake happens, while the comparison that decides
         * whether to bake lives somewhere else and is not updated - so the bake
         * silently stops happening and nothing says so. Here the comparison is
         * the signature's own `operator==` and one place records it, so the two
         * cannot drift apart.
         *
         * @tparam Signature Everything the bake depends on, comparable for equality.
         */
        template <typename Signature>
        class BakedFrom {
            public:
                BakedFrom()  = default;
                ~BakedFrom() = default;

                BakedFrom(const BakedFrom& other) = default;
                BakedFrom& operator=(const BakedFrom& other) = default;

                BakedFrom(BakedFrom && other) = default;
                BakedFrom& operator=(BakedFrom && other) = default;

            public:
                /**
                 * @brief Whether @p now differs from what is baked; adopts it if so.
                 *
                 * @param now What this frame would bake from.
                 * @return Whether the bake has to run.
                 */
                bool changed(const Signature& now) {
                    if (m_baked && m_last == now) return false;
                    m_last  = now;
                    m_baked = true;
                    return true;
                }

                /// Forget what was baked, so the next changed() answers true.
                void invalidate() { m_baked = false; }

            private:
                Signature m_last{};
                bool      m_baked = false;
        };

        /**
         * @brief Everything the procedural sky bake depends on.
         */
        struct SkySignature {
            glm::vec3 sunDir{0.0f};
            float     sunIntensity = 0.0f;
            float     rayleigh     = 0.0f;
            float     mie          = 0.0f;
            float     mieG         = 0.0f;
            glm::vec3 nightRadiance{0.0f};
            glm::vec3 moonDir{0.0f};
            float     moonIntensity = 0.0f;

            /**
             * @brief Whether two skies are the same sky.
             *
             * Every term exact except the sun direction, which is compared by
             * angle: it moves continuously, and re-baking a cubemap for a
             * thousandth of a degree is a bake every frame the sun is animated.
             *
             * @param other The sky to compare against.
             * @return Whether a bake of @p other would produce this one.
             */
            bool operator==(const SkySignature& other) const {
                return sunIntensity  == other.sunIntensity
                    && rayleigh      == other.rayleigh
                    && mie           == other.mie
                    && mieG          == other.mieG
                    && nightRadiance == other.nightRadiance
                    && moonDir       == other.moonDir
                    && moonIntensity == other.moonIntensity
                    && glm::dot(sunDir, other.sunDir) >= 0.99995f;
            }
        };

        /**
         * @brief Everything the irradiance-volume bake depends on.
         */
        struct IrradianceSignature {
            glm::vec3 center{0.0f};
            glm::vec3 halfExtents{0.0f};
            uint32_t  resolutionX = 0, resolutionY = 0, resolutionZ = 0;
            uint32_t  bakeVersion = 0;

            bool operator==(const IrradianceSignature& other) const {
                return center      == other.center
                    && halfExtents == other.halfExtents
                    && resolutionX == other.resolutionX
                    && resolutionY == other.resolutionY
                    && resolutionZ == other.resolutionZ
                    && bakeVersion == other.bakeVersion;
            }
        };

        /**
         * @brief The sky the environment describes, as a bake signature.
         *
         * @param env The scene's environment.
         * @param sunDir Direction to the sun, already derived from its angles.
         * @return What baking that environment's procedural sky depends on.
         */
        static SkySignature skySignature(const Environment& env, const glm::vec3& sunDir);

    private:
        ConstantsInstalled m_constants;   ///< First: everything below may compile a shader.

        Vkm::GL::Context m_context;
        GLView           m_view;

        // Identities the caches above were built against. See onWorldReplaced.
        uint64_t      m_assetEpoch = 0;
        uint64_t      m_worldEpoch = 0;

        // Batches the opaque bucket once per frame for both the depth prepass
        // and the forward pass (see GLFrameContext::opaqueBatch).
        GLInstanceBatcher m_opaqueBatcher;
        ScreenTriangle m_screenTri;  ///< Shared fullscreen triangle, referenced by the frame context.
        /// Single-sample resolved scene (sampled by post). At 1x MSAA the geometry passes render straight into it.
        GLTarget m_sceneHDR{GLTarget::Layout::ColorDepthGBuffer};
        /// Multisample scene the geometry passes render into when MSAA is on; resolved into m_sceneHDR.
        GLTarget m_sceneMS{GLTarget::Layout::ColorDepthGBuffer};
        GLTarget m_postA{GLTarget::Layout::Color};   ///< Post scratch (ping).
        GLTarget m_postB{GLTarget::Layout::Color};   ///< Post scratch (pong).
        GLTarget m_ao{GLTarget::Layout::Color};      ///< GTAO factor + packed bent normal.

        /// Images the host's own chrome asked for, by path. Uploaded once each,
        /// and never touched by a project open - they are the engine's.
        std::unordered_map<std::string, std::unique_ptr<Vkm::GL::Texture2D>> m_chromeImages;

        GLCamera      m_camera;
        GLLights      m_lights;

        GLShadowAtlas m_shadowAtlas;
        GLShadowData  m_shadowData;

        // The rig every offline bake draws with, lent to the three bakers below:
        // one compile of the PBR ubershader and the convolution programs rather
        // than one per baker. Declared first, because they bind references to it.
        GLSceneCapture  m_sceneCapture;
        GLCubeConvolver m_cubeConvolver;

        GLIBL         m_ibl;
        GLIBLBaker    m_iblBaker;   ///< Persistent: the procedural sky re-bakes whenever the sun moves.
        GLBloom       m_bloom;
        GLHiZ         m_hiz;
        GLSkinPalette m_skinPalette;  ///< This frame's bone palettes, in one storage buffer.
        GLClusterGrid m_clusterGrid;  ///< Forward+ per-cluster light lists (compute-filled).
        GLFogVolume   m_fog;          ///< Froxel volumetric-fog volumes (compute-filled).
        GLIrradianceVolume m_irradiance;      ///< Baked SH irradiance volume (GI).
        GLIrradianceBaker  m_irradianceBaker;

        GLProbeManager m_probes;   ///< Reflection-probe arrays, baker, bake state, UBO.
        GLPreview      m_preview;  ///< Editor material/mesh preview renders.

        // Per-frame draw buckets - cleared + refilled each frame, capacity kept.
        std::vector<const DrawableData*> m_opaque;
        std::vector<const DrawableData*> m_alphaMask;
        std::vector<const DrawableData*> m_transparent;

        std::vector<PassEntry> m_passes;

        std::string m_bakedEnvPath;  ///< HDR path of the currently baked IBL; empty when none (or the sky is procedural).

        BakedFrom<SkySignature>        m_bakedSky;
        BakedFrom<IrradianceSignature> m_bakedIrradiance;
};

} // namespace Vkm::Engine
