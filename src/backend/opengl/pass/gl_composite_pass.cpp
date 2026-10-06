#include "pass/gl_composite_pass.h"

#include <cmath>

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"
#include "gl_screen_triangle.h"
#include "gl_frame_buffer.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "storage/gl_bloom.h"
#include "storage/gl_shadow_atlas.h"
#include "convention/gl_bindings.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLCompositePass::GLCompositePass()
    : m_shader("shaders/composite") {}

GLCompositePass::~GLCompositePass() = default;

void GLCompositePass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;

    bindBackbufferViewport(ctx);
    ctx.gl.setDepthTest(false);

    m_shader.bind();
    ctx.colorSrc->bindTexture(GLTarget::Attachment::Color, GLBindings::CompositeTextureSlots::SCENE);
    ctx.bloom.bind(GLBindings::CompositeTextureSlots::BLOOM);
    m_shader.setUniform1f("u_bloomStrength", ctx.bloomReady ? view.settings.bloomStrength : 0.0f);
    m_shader.setUniform1i("u_tonemap", static_cast<int>(view.settings.tonemap));
    m_shader.setUniform1f("u_exposure", std::exp2(view.settings.exposure));

    // The debug views sample the intermediate buffers.
    const int mode = static_cast<int>(view.settings.renderMode);
    m_shader.setUniform1i("u_renderMode", mode);
    if (mode != static_cast<int>(RenderMode::Default)) {
        ctx.sceneHDR.bindTexture(GLTarget::Attachment::Depth, GLBindings::PostTextureSlots::SCENE_DEPTH);
        ctx.sceneHDR.bindTexture(GLTarget::Attachment::GBuffer, GLBindings::PostTextureSlots::SCENE_GBUFFER);
        bindAO(ctx, m_shader);
        bindFog(ctx, m_shader);
    }

    // Unconditional: the driver validates this plain sampler2D whether or not the debug branch
    // reads it. Its own unit, since the shadow readers' unit carries a comparing sampler.
    ctx.shadowAtlas.bind2DRaw(GLBindings::ShadowTextureSlots::ATLAS_2D_RAW);

    ctx.screenTri.draw();
}

} // namespace Vkm::Engine
