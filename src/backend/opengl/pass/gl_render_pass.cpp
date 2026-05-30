#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_render_pass.h"

#include "logger.h"
#include "debug/profiler_gl.h"

#include "core/gl_backend.h"
#include "system/render/render_backend.h"

namespace Engine {

void GLRenderPass::execute(RenderGraphContext& ctx) {
    PROFILE_GPU_SCOPE_NAMED(getName().c_str());
    if (ctx.backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("%s requires OpenGL backend, got %s - skipping pass",
                  getName().c_str(), toString(ctx.backend.getType()));
        return;
    }
    executeGL(static_cast<GLBackend&>(ctx.backend), ctx);
}

} // namespace Engine
