#include "gl_pass.h"

#include "gl_context.h"
#include "gl_frame_buffer.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

void GLPass::beginFullscreen(Vkm::GL::Context& gl) const {
    gl.setDepthTest(false);
    gl.setBlending(false);
    gl.setFaceCulling(false);
}

void GLPass::endFullscreen(Vkm::GL::Context& gl) const {
    gl.setDepthTest(true);
}

void GLPass::promoteColorChain(GLFrameContext& ctx) const {
    if (ctx.colorSrc != &ctx.sceneHDR) return;
    ctx.colorDst->blitColorFrom(*ctx.colorSrc);
    ctx.flipColor();
}

void GLPass::bindBackbufferViewport(GLFrameContext& ctx) const {
    const RenderView& view = ctx.view;
    const int32_t glY = static_cast<int32_t>(view.surfaceHeight)
                      - static_cast<int32_t>(view.viewportY)
                      - static_cast<int32_t>(view.viewportHeight);
    Vkm::GL::FrameBuffer::bindDefault();
    ctx.gl.setViewport(
        static_cast<int32_t>(view.viewportX),
        glY,
        static_cast<int32_t>(view.viewportWidth),
        static_cast<int32_t>(view.viewportHeight)
    );
}

} // namespace Vkm::Engine
