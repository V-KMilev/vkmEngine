#include "pass/gl_ocean_pass.h"

#include "gl_frame_context.h"
#include "storage/gl_ocean_fields.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

void GLOceanPass::execute(GLFrameContext& ctx) {
    if (!ctx.view.ocean.present) return;
    if (ctx.view.ocean.ripples.any()) ctx.oceanFields.drawRipples();
    if (ctx.view.hasCamera && ctx.view.ocean.waterline > 0.0f) ctx.oceanFields.captureShore(ctx);
}

} // namespace Vkm::Engine
