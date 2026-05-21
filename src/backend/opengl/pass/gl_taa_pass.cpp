#include "gl_taa_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "logger.h"
#include "debug/print_helper.h"

#include "core/gl_backend.h"
#include "core/gl_hdr_target.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_gbuffer.h"
#include "resource/gl_taa.h"

#include "gl_screen_triangle.h"
#include "gl_blit.h"

#include "system/render/render_view.h"
#include "resource/resource_manager.h"

namespace Engine {

bool GLTAAPass::enabledForView(const RenderView& view) const {
    return isEnabled() && view.environment.taa && !view.environment.wireframe;
}

GLTAAPass::GLTAAPass(ShaderHandle shader)
    : RenderPass("GLTAAPass")
    , m_shader(shader)
    , m_screenTri(std::make_unique<Core::ScreenTriangle>())
{
}

GLTAAPass::~GLTAAPass() = default;

void GLTAAPass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // GLTAA history is owned and resized by GLBackend's FrameResources.
}

void GLTAAPass::execute(RenderGraphContext& rg) {
    RenderBackend& backend = rg.backend;
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;

    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLTAAPass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }
    auto& gl      = static_cast<GLBackend&>(backend);
    auto& hdr = *rg.resource<GLHdrTarget>(RGResource::SceneHDR);
    auto& gbuffer = *rg.resource<GLGBuffer>(RGResource::GBufferNormal);
    auto& taa = *rg.resource<GLTAA>(RGResource::TAAHistory);
    if (!hdr.isReady() || !gbuffer.isReady() || !taa.isReady()) return;

    GLShader* shader = gl.getView().resolveShader(m_shader, resources);
    if (!shader) return;

    auto& ctx = gl.getContext();
    ctx.setDepthTest(false);
    ctx.setDepthWrite(false);
    ctx.setBlending(false);
    ctx.setFaceCulling(false);

    // Accumulate into the write history texture.
    taa.bindWrite();

    shader->bind();
    shader->setUniformMatrix4fv("u_invView", glm::inverse(view.camera.view));
    shader->setUniformMatrix4fv("u_prevViewProj",
        m_havePrev ? m_prevViewProj : view.camera.viewProjection);
    shader->setUniform1f("u_blend", view.environment.taaBlend);
    shader->setUniform1i("u_primed", (m_havePrev && taa.primed()) ? 1 : 0);

    hdr.bindResolvedColor(0);
    taa.bindHistory(1);
    gbuffer.bindPosition(2);

    m_screenTri->draw();

    // Substitute the stabilised image for the downstream post chain.
    Core::blitColor(taa.fboId(), hdr.resolveFboId(),
        static_cast<int>(taa.width()), static_cast<int>(taa.height()),
        static_cast<int>(hdr.width()), static_cast<int>(hdr.height()), GL_NEAREST);

    taa.swap();
    taa.markPrimed();
    m_prevViewProj = view.camera.viewProjection;
    m_havePrev = true;

    ctx.setDepthTest(true);
    ctx.setDepthWrite(true);
}

} // namespace Engine
