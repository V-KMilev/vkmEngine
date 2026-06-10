#pragma once

namespace Core {
    class Context;
}

namespace Engine {

struct RenderView;
class GLView;
class GLTarget;

/**
 * @brief Everything a GLPass needs for one frame.
 *
 * The backend builds one of these per frame and hands it to each pass: the
 * scene snapshot, the GPU resource mirror, the GL state manager, and the render
 * targets. New targets (a shadow atlas, post buffers) get added here as fields,
 * so the GLPass::execute signature never has to change again.
 */
struct GLFrameContext {
    const RenderView& view;       ///< This frame's scene snapshot.
    const GLView&     resources;  ///< GPU mirror of the assets the frame uses.
    Core::Context&    gl;         ///< GL state manager (viewport / depth / clear).
    GLTarget&         sceneHDR;   ///< HDR target the forward pass draws into.
};

} // namespace Engine
