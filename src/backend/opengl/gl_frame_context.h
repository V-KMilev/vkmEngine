#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "data/gl_instance_batcher.h"

namespace Core {
    class Context;
    class ScreenTriangle;
}

namespace Engine {

struct RenderView;
struct DrawableData;
class GLView;
class GLTarget;
class GLMaskTarget;
class GLShadowAtlas;
class GLShadowData;
class GLIBL;
class GLBloom;
class GLClusterGrid;
class GLFogVolume;
class GLIrradianceVolume;

/**
 * @brief Everything a GLPass needs for one frame.
 *
 * The backend builds one of these per frame and hands it to each pass: the
 * scene snapshot, the GPU resource mirror, the GL state manager, and the render
 * targets. New targets (a shadow atlas, post buffers) get added here as fields,
 * so the GLPass::execute signature never has to change again.
 */
struct GLFrameContext {
    const RenderView& view;           ///< This frame's scene snapshot.
    const GLView&     resources;      ///< GPU mirror of the assets the frame uses.
    Core::Context&    gl;             ///< GL state manager (viewport / depth / clear).
    Core::ScreenTriangle& screenTri;  ///< Shared attribute-less fullscreen triangle (every post pass draws it).
    GLTarget&         sceneHDR;       ///< Single-sample resolved scene: sampled by the screen-space + post passes.
    GLTarget&         sceneRender;    ///< Where the geometry passes draw (the multisample target, or sceneHDR itself when MSAA is off). Resolved into sceneHDR by GLResolvePass.
    GLShadowAtlas&    shadowAtlas;    ///< Depth atlas: written by shadow pass, sampled by forward.
    const GLShadowData& shadowData;   ///< This frame's shadow plan (matrices + slots).
    const GLIBL&      ibl;            ///< Baked IBL product set: sampled by forward (ambient) + skybox.
    GLBloom&          bloom;          ///< Bloom mip chain: written by the bloom pass, blended in composite.
    GLMaskTarget&       ao;             ///< GTAO factor: written by the GTAO pass, sampled by forward (ambient).
    GLMaskTarget&       contactShadow;  ///< Sun contact-shadow mask: written by the contact-shadow pass, sampled by forward.
    GLClusterGrid&    clusters;       ///< Forward+ per-cluster light lists: written by the cluster pass, read by forward.
    GLFogVolume&      fog;            ///< Froxel fog volumes: written by the fog compute, applied by the fog-apply pass.
    GLIrradianceVolume& irradiance;   ///< Baked SH irradiance volume, sampled by the forward ambient term.

    /**
     * @brief The frame's drawables split by draw bucket, once per frame by the
     * backend (one material resolve each, not one per consuming pass). Opaque
     * (incl. Unlit) is shared by the depth prepass + forward in view order.
     * AlphaMask skips the prepass and draws in the forward pass with depth
     * writes on + alpha-to-coverage (so its edges anti-alias under MSAA).
     * Transparent is forward-only, sorted back-to-front there.
     */
    const std::vector<const DrawableData*>& opaque;
    const std::vector<const DrawableData*>& alphaMask;
    const std::vector<const DrawableData*>& transparent;

    /**
     * @brief The opaque bucket already batched into instanced runs.
     *
     * A frame product like the buckets themselves, for the same reason: the
     * depth prepass and the forward pass draw the identical opaque list, so
     * batching it per-pass sorted ~5000 drawables and re-uploaded both instance
     * buffers twice a frame for byte-identical results.
     *
     * Consumers draw its runs through it and must not call build*() on it -
     * that would invalidate the runs the other pass is about to draw. A pass
     * needing its own batching (alpha-mask, transparent) keeps a private
     * batcher.
     */
    GLInstanceBatcher& opaqueBatch;

    // --- Filled by the backend before the pass loop -------------------------

    /**
     * @brief The post-processing colour chain.
     *
     * colorSrc holds the scene as of the last completed pass (it starts at
     * sceneHDR); colorDst is the free colour-only scratch the next pass writes.
     * A post pass samples colorSrc, draws into colorDst, then calls flipColor().
     * After the first flip the chain ping-pongs between the two scratches and
     * never writes sceneHDR again - so no pass ever samples a texture attached
     * to its own draw framebuffer, and nothing is ever blitted back.
     */
    GLTarget* colorSrc = nullptr;
    GLTarget* colorDst = nullptr;

    /**
     * @brief The two colour-only scratch targets flipColor() alternates between.
     * Also usable as transient copy space by a pass that has not entered the
     * chain yet (the forward pass's refraction grab uses colorDst that way).
     */
    GLTarget* scratchA = nullptr;
    GLTarget* scratchB = nullptr;

    /**
     * @brief Publish colorDst as the current scene and aim colorDst at the
     * scratch not now holding it.
     */
    void flipColor() {
        colorSrc = colorDst;
        colorDst = (colorDst == scratchA) ? scratchB : scratchA;
    }

    /**
     * @brief Reflection probes the backend bound this frame: boxes/layers in the
     * ProbeBlock UBO, cubes in the two probe arrays. The forward pass passes
     * this count to the shader's per-fragment blend loop (0 = none).
     */
    int probeCount = 0;

    /**
     * @brief Direction TO the sun (the scene's primary directional light, or a
     * default when there is none). Used by the procedural sky bake and the
     * skybox sun disc.
     */
    glm::vec3 sunDir{0.0f, 1.0f, 0.0f};

    /**
     * @brief Whether sunDir came from a real directional light this frame (false =
     * the fabricated default). Sun-only passes (contact shadows) skip when false.
     */
    bool hasSun = false;

    // --- Pass products: set by an earlier pass, read by later ones ----------

    /**
     * @brief Set by the GTAO pass when it fills the AO target. The forward pass then
     * binds + samples it; otherwise the indirect term uses no screen-space AO.
     */
    bool aoReady = false;

    /**
     * @brief Set by the contact-shadow pass when it fills the mask. The forward
     * pass then multiplies it into the sun; otherwise the sun is un-contact-shadowed.
     */
    bool contactShadowReady = false;

    /**
     * @brief Set by the fog compute when it fills + integrates the froxel volume.
     * The fog-apply pass composites only when set, so it can never sample a
     * stale volume if the two passes' conditions ever drift apart.
     */
    bool fogReady = false;
};

} // namespace Engine
