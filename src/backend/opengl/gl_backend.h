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
#include "data/gl_ibl.h"
#include "data/gl_ibl_baker.h"
#include "data/gl_bloom.h"
#include "data/gl_hiz.h"
#include "data/gl_cluster_grid.h"
#include "data/gl_fog_volume.h"
#include "data/gl_irradiance_volume.h"
#include "data/gl_irradiance_baker.h"
#include "data/gl_probe_manager.h"
#include "data/gl_preview.h"

namespace Engine {
    class GLPass;
    struct DrawableData;
    struct Environment;
}

namespace Engine {
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
 * list of passes. render() syncs the frame's resources, then runs each pass in
 * order (clearing is delegated to the passes). The window's buffer swap stays in the engine loop (it must
 * happen after the editor UI draws), so this backend draws but does not present.
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
        bool init(WindowManager& window) override;
        void resize(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        void render(const RenderView& view, const ResourceManager& resources) override;

        // Editor previews: offscreen studio renders, cached per key (GLPreview).
        GpuTextureId renderPreview(const PreviewRequest& request,
                               const ResourceManager& resources) override;
        GpuTextureId previewTexture(uint64_t key) const override;
        void releasePreview(uint64_t key) override;
        void releaseAllPreviews() override;
        GpuTextureId textureId(const TextureHandle& handle) const override;
        uint32_t reloadChangedShaders() override;

    private:
        /**
         * @brief Drop every GPU cache derived from the scene, after a graph swap.
         *
         * Several caches avoid redundant GPU work by remembering what they last
         * built and comparing it against values the scene supplies: asset handle
         * and version for GLView, probe position and bakeVersion for the probe
         * array, box and grid for the irradiance volume. Every one of those
         * repeats exactly when a scene is replaced - SceneSerializer commits by
         * swapping a freshly built graph into place, and the editor's play-stop
         * restore takes the same path - so each would skip work it must redo and
         * go on showing the previous scene's contents.
         *
         * Detecting the swap here, once, is deliberate. The alternative is every
         * cache inventing its own staleness test, which is exactly how the probe
         * array and the irradiance volume came to be missed when GLView was
         * fixed. A new scene-derived cache belongs in this function.
         *
         * @param resources The frame's resource manager, carrying the epoch.
         */
        void onAssetGraphSwapped(const ResourceManager& resources);

        /**
         * @brief Split the frame's drawables into the opaque + transparent buckets once
         * (one material resolve each) so the depth prepass and forward pass share
         * the result instead of re-partitioning the list a pass apiece.
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
         * @brief True when the procedural sky must be re-baked: the sun moved or a
         * sky parameter changed since the last procedural bake.
         */
        bool skyNeedsRebake(const Environment& env, const glm::vec3& sunDir) const;

    private:
        Core::Context m_context;
        GLView        m_view;

        // Asset-graph identity the scene-derived GPU caches were built against.
        // See onAssetGraphSwapped.
        uint64_t      m_assetEpoch = 0;

        // Batches the opaque bucket once per frame for both the depth prepass
        // and the forward pass (see GLFrameContext::opaqueBatch).
        GLInstanceBatcher m_opaqueBatcher;
        Core::ScreenTriangle m_screenTri;  ///< Shared fullscreen triangle, referenced by the frame context.
        GLTarget      m_sceneHDR;    ///< Single-sample resolved scene (sampled by post). At 1x MSAA the geometry passes render straight into it.
        GLTarget      m_sceneMS;     ///< Multisample scene the geometry passes render into when MSAA is on; resolved into m_sceneHDR.
        GLTarget      m_postA;   ///< Colour-only post scratch (ping).
        GLTarget      m_postB;   ///< Colour-only post scratch (pong).
        GLTarget      m_ao;      ///< Colour-only GTAO factor target.

        GLCamera      m_camera;
        GLLights      m_lights;

        GLShadowAtlas m_shadowAtlas;
        GLShadowData  m_shadowData;

        GLIBL         m_ibl;
        GLIBLBaker    m_iblBaker;   ///< Persistent: the procedural sky re-bakes whenever the sun moves.
        GLBloom       m_bloom;
        GLHiZ         m_hiz;
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

        /**
         * @brief Signature of the procedural sky currently baked into m_ibl, so a
         * frame re-bakes only when the sun or a parameter actually changes.
         */
        struct BakedSky {
            bool      active = false;
            glm::vec3 sunDir{0.0f};
            float     sunIntensity = 0.0f;
            float     rayleigh     = 0.0f;
            float     mie          = 0.0f;
            float     mieG         = 0.0f;
            glm::vec3 nightRadiance{0.0f};
            glm::vec3 moonDir{0.0f};
            float     moonIntensity = 0.0f;
        };
        BakedSky m_bakedSky;

        /**
         * @brief Signature of the irradiance volume currently baked, so a frame
         * re-bakes only when the box, grid, or bake version actually changes.
         */
        struct BakedIrradiance {
            bool      valid = false;
            glm::vec3 center{0.0f};
            glm::vec3 halfExtents{0.0f};
            uint32_t  resolutionX = 0, resolutionY = 0, resolutionZ = 0;
            uint32_t  bakeVersion = 0;
        };
        BakedIrradiance m_bakedIrradiance;
};

} // namespace Engine
