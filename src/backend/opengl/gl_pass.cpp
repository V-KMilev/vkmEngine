#include "gl_pass.h"

#include "gl_context.h"
#include "gl_frame_buffer.h"

#include "gl_frame_context.h"
#include "system/render/render_view.h"

namespace Engine {

void GLPass::beginFullscreen(Core::Context& gl) const {
    gl.setDepthTest(false);
    gl.setBlending(false);
    gl.setFaceCulling(false);
}

void GLPass::endFullscreen(Core::Context& gl) const {
    gl.setDepthTest(true);
}

void GLPass::bindBackbufferViewport(GLFrameContext& ctx) const {
    const RenderView& view = ctx.view;
    const int32_t glY = static_cast<int32_t>(view.surfaceHeight)
                      - static_cast<int32_t>(view.viewportY)
                      - static_cast<int32_t>(view.viewportHeight);
    Core::FrameBuffer::bindDefault();
    ctx.gl.setViewport(
        static_cast<int32_t>(view.viewportX),
        glY,
        static_cast<int32_t>(view.viewportWidth),
        static_cast<int32_t>(view.viewportHeight)
    );
}

} // namespace Engine
