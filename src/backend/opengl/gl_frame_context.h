#pragma once

#include <vector>

namespace Core {
    class Context;
}

namespace Engine {

struct RenderView;
struct DrawableData;
class GLView;
class GLTarget;
class GLAOTarget;
class GLShadowAtlas;
class GLShadowData;
class GLIBL;
class GLBloom;

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
    GLTarget&         sceneHDR;       ///< HDR target the forward pass draws into.
    GLTarget&         sceneColor;     ///< Copy of the opaque+sky scene for transmission refraction.
    GLShadowAtlas&    shadowAtlas;    ///< Depth atlas: written by shadow pass, sampled by forward.
    const GLShadowData& shadowData;   ///< This frame's shadow plan (matrices + slots).
    const GLIBL&      ibl;            ///< Baked IBL product set: sampled by forward (ambient) + skybox.
    GLBloom&          bloom;          ///< Bloom mip chain: written by the bloom pass, blended in composite.
    GLAOTarget&       ao;             ///< GTAO factor: written by the GTAO pass, sampled by forward (ambient).

    /// The frame's drawables split by draw bucket, once per frame by the
    /// backend (one material resolve each, not one per consuming pass). Opaque
    /// (incl. AlphaMask / Unlit) is shared by the depth prepass + forward in
    /// view order; transparent is forward-only, sorted back-to-front there.
    const std::vector<const DrawableData*>& opaque;
    const std::vector<const DrawableData*>& transparent;

    /// Reflection probes the backend bound this frame: boxes/layers in the
    /// ProbeBlock UBO, cubes in the two probe arrays. The forward pass passes
    /// this count to the shader's per-fragment blend loop (0 = none).
    int probeCount = 0;

    /// Set by the depth prepass when it lays down opaque depth. The forward pass
    /// then early-Zs (LEQUAL, no depth writes/clear) instead of clearing depth.
    bool depthPrimed = false;

    /// Set by the GTAO pass when it fills the AO target. The forward pass then
    /// binds + samples it; otherwise the indirect term uses no screen-space AO.
    bool aoReady = false;
};

} // namespace Engine
