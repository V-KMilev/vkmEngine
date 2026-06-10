#pragma once

#include <glm/glm.hpp>

namespace Core {
    class Context;
}

namespace Engine {

struct RenderView;
class GLView;
class GLTarget;
class GLAOTarget;
class GLShadowAtlas;
class GLShadowData;
class GLIBL;
class GLBloom;
class GLProbe;

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

    /// Nearest reflection probe covering the frame (null = none). The forward
    /// pass blends it over the global IBL inside the box below.
    const GLProbe* probe          = nullptr;
    glm::vec3      probeCenter     = glm::vec3(0.0f);  ///< Probe box centre (world).
    glm::vec3      probeExtents    = glm::vec3(1.0f);  ///< Probe box half-extents (world).
    float          probeFalloff   = 0.2f;             ///< Edge falloff fraction.
    float          probeIntensity = 1.0f;             ///< Contribution multiplier.

    /// Set by the depth prepass when it lays down opaque depth. The forward pass
    /// then early-Zs (LEQUAL, no depth writes/clear) instead of clearing depth.
    bool depthPrimed = false;

    /// Set by the GTAO pass when it fills the AO target. The forward pass then
    /// binds + samples it; otherwise the indirect term uses no screen-space AO.
    bool aoReady = false;
};

} // namespace Engine
