#include "pass/gl_ocean_pass.h"

#include "gl_frame_context.h"
#include "frame/gl_ocean.h"
#include "storage/gl_ocean_fields.h"
#include "storage/gl_ocean_shore.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

void GLOceanPass::execute(GLFrameContext& ctx) {
    const OceanData& ocean = ctx.view.ocean;
    if (GLOcean::rippling(ocean, ctx.waterShown)) ctx.oceanFields.drawRipples();
    if (!ocean.present) return;
    if (ctx.view.hasCamera && ocean.waterline > 0.0f) ctx.oceanShore.capture(ctx);
}

} // namespace Vkm::Engine
