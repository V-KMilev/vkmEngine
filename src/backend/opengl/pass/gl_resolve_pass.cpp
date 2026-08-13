#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_resolve_pass.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "system/render/render_view.h"

namespace Engine {

void GLResolvePass::execute(GLFrameContext& ctx) {
    // MSAA off: the render target and the resolved target are the same object,
    // so there is nothing to resolve.
    if (&ctx.sceneRender == &ctx.sceneHDR) return;

    if (m_scope == Scope::Geometry) {
        // The resolved G-buffer only feeds GTAO, decals and the composite debug
        // views - skip that full-res blit when none of them will read it (the
        // depth half always resolves; fog and DoF need it).
        const RenderView& view = ctx.view;
        const bool needGBuffer = view.settings.gtao
                              || !view.decals.empty()
                              || view.settings.renderMode != RenderMode::Default;
        ctx.sceneRender.resolveGeometryTo(ctx.sceneHDR, needGBuffer);
    } else {
        ctx.sceneRender.resolveColorTo(ctx.sceneHDR);

        // Alpha-masked geometry skips the prepass and writes its depth in the
        // forward pass - after the geometry resolve above. Re-resolve depth so
        // the post passes (decals, fog, DoF) see cutouts exactly
        // as they do with MSAA off, instead of fogging/blurring them as
        // background. A pure-opaque frame skips it: depth is unchanged.
        if (!ctx.alphaMask.empty())
            ctx.sceneRender.resolveGeometryTo(ctx.sceneHDR, false);
    }
}

} // namespace Engine
