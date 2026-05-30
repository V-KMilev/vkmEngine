#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_taa_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "logger.h"
#include "debug/print_helper.h"
#include "debug/profiler_gl.h"

#include "core/gl_backend.h"
#include "core/gl_scene_target.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_gbuffer.h"
#include "resource/gl_taa.h"

#include "gl_screen_triangle.h"
#include "gl_blit.h"
#include "gl_fullscreen_post.h"

#include "system/render/render_view.h"
#include "resource/resource_manager.h"

namespace Engine {

bool GLTAAPass::enabledForView(const RenderView& view) const {
    return isEnabled() && view.environment.taa.enabled && !view.modeConfig.disablePost;
}

GLTAAPass::GLTAAPass(ShaderHandle shader)
    : GLRenderPass("GLTAAPass")
    , m_shader(shader)
    , m_screenTri(std::make_unique<Core::ScreenTriangle>())
{
}

GLTAAPass::~GLTAAPass() = default;

void GLTAAPass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // GLTAA history is owned and resized by GLBackend's FrameResources.
}

void GLTAAPass::executeGL(GLBackend& gl, RenderGraphContext& rg) {
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    auto& hdr = *rg.resource<GLSceneTarget>(RGResource::SceneHDR);
    auto& gbuffer = *rg.resource<GLGBuffer>(RGResource::GBufferNormal);
    auto& taa = *rg.resource<GLTAA>(RGResource::TAAHistory);
    if (!hdr.isReady() || !gbuffer.isReady() || !taa.isReady()) return;

    GLShader* shader = gl.getView().resolveShader(m_shader, resources);
    if (!shader) return;

    beginFullscreenPost(gl.getContext());

    // Accumulate into the write history texture.
    taa.bindWrite();

    shader->bind();
    shader->setUniformMatrix4fv("u_invView", glm::inverse(view.camera.view));
    shader->setUniformMatrix4fv("u_prevViewProj",
        m_havePrev ? m_prevViewProj : view.camera.viewProjection);
    shader->setUniform1f("u_blend", view.environment.taa.blend);
    shader->setUniform1i("u_primed", (m_havePrev && taa.primed()) ? 1 : 0);

    hdr.bindResolvedColor(0);
    taa.bindHistory(1);
    gbuffer.bindPosition(2);

    m_screenTri->draw();

    // Substitute the stabilised image for the downstream post chain.
    endFullscreenPost(gl.getContext(),
        taa.fboId(), static_cast<int>(taa.width()), static_cast<int>(taa.height()),
        hdr.resolveFboId(), static_cast<int>(hdr.width()), static_cast<int>(hdr.height()));

    taa.swap();
    taa.markPrimed();
    m_prevViewProj = view.camera.viewProjection;
    m_havePrev = true;
}

} // namespace Engine
