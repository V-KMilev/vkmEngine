#pragma once

namespace Core {
    class Context;
}

namespace Engine {

struct RenderView;
class GLView;
class GLTarget;
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
    GLShadowAtlas&    shadowAtlas;    ///< Depth atlas: written by shadow pass, sampled by forward.
    const GLShadowData& shadowData;   ///< This frame's shadow plan (matrices + slots).
    const GLIBL&      ibl;            ///< Baked IBL product set: sampled by forward (ambient) + skybox.
    GLBloom&          bloom;          ///< Bloom mip chain: written by the bloom pass, blended in composite.

    /// Set by the depth prepass when it lays down opaque depth. The forward pass
    /// then early-Zs (LEQUAL, no depth writes/clear) instead of clearing depth.
    bool depthPrimed = false;
};

} // namespace Engine
