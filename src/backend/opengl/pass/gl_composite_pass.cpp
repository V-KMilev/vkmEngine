#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_composite_pass.h"

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"
#include "data/gl_screen_triangle.h"
#include "gl_frame_buffer.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "data/gl_bloom.h"
#include "data/gl_fog_volume.h"
#include "data/gl_shadow_atlas.h"
#include "convention/gl_bindings.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLCompositePass::GLCompositePass()
    : m_shader(std::make_unique<Vkm::GL::Shader>("shaders/composite")) {}

GLCompositePass::~GLCompositePass() = default;

void GLCompositePass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;

    bindBackbufferViewport(ctx);
    beginFullscreen(ctx.gl);

    m_shader->bind();
    ctx.colorSrc->bindColor(GLBindings::CompositeTextureSlots::Scene);
    ctx.bloom.bind(GLBindings::CompositeTextureSlots::Bloom);
    const float bloomStrength = (ctx.bloom.isReady() && view.settings.bloom)
        ? view.settings.bloomStrength : 0.0f;
    m_shader->setUniform1f("u_bloomStrength", bloomStrength);

    // The debug views sample the intermediate buffers; the projection goes with
    // them for depth linearization.
    const int mode = static_cast<int>(view.settings.renderMode);
    m_shader->setUniform1i("u_renderMode", mode);
    if (mode != static_cast<int>(RenderMode::Default)) {
        ctx.sceneHDR.bindDepth(GLBindings::PostTextureSlots::SceneDepth);
        ctx.sceneHDR.bindGBuffer(GLBindings::PostTextureSlots::SceneGBuffer);
        // The AO target is allocated for any debug view, but only the GTAO pass
        // ever writes it: with GTAO off the AO view would show whatever the
        // allocation happened to contain. Show the unoccluded value instead.
        if (ctx.aoReady) ctx.ao.bindColor(GLBindings::PostTextureSlots::SSAO);
        m_shader->setUniform1i("u_hasAO", ctx.aoReady ? 1 : 0);
        ctx.shadowAtlas.bind2D(GLBindings::ShadowTextureSlots::Atlas2D);
        if (ctx.fogReady)
            ctx.fog.bindIntegratedSlot(GLBindings::PostTextureSlots::FogVolume);
        m_shader->setUniformMatrix4fv("u_projection", view.camera.projection);
    }

    ctx.screenTri.draw();
}

} // namespace Vkm::Engine
