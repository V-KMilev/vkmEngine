#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_hiz_pass.h"

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"
#include "gl_screen_triangle.h"

#include "convention/gl_bindings.h"
#include "data/gl_hiz.h"
#include "gl_frame_context.h"
#include "gl_target.h"
#include "system/render/render_view.h"

namespace Engine {

GLHiZPass::GLHiZPass()
    : m_reduce(std::make_unique<Core::Shader>("shaders/hiz/reduce")) {}

GLHiZPass::~GLHiZPass() = default;

void GLHiZPass::execute(GLFrameContext& ctx) {
    GLHiZ& hiz = ctx.hiz;
    if (!hiz.isReady()) return;

    beginFullscreen(ctx.gl);
    ctx.screenTri.bind();
    hiz.bindFbo();

    m_reduce->bind();

    const int mips = hiz.mipCount();
    for (int mip = 0; mip < mips; ++mip) {
        if (mip == 0) {
            // Level 0 reduces the scene depth itself. It is a different texture
            // but the same sampler2D and the same .r, so the loop does not
            // special-case anything past which texture is bound.
            ctx.sceneHDR.bindDepth(GLBindings::PostTextureSlots::SceneDepth);
            m_reduce->setUniform1i("u_src", static_cast<int>(GLBindings::PostTextureSlots::SceneDepth));
            m_reduce->setUniform1f("u_srcLod", 0.0f);
            m_reduce->setUniform2i("u_srcSize", static_cast<int>(ctx.view.viewportWidth),
                                                static_cast<int>(ctx.view.viewportHeight));
        } else {
            // Reading level mip-1 while writing level mip is only defined while
            // the sampled range stops below the attachment.
            hiz.restrictSampling(mip - 1);
            hiz.bind(GLBindings::PostTextureSlots::HiZ);
            m_reduce->setUniform1i("u_src", static_cast<int>(GLBindings::PostTextureSlots::HiZ));
            m_reduce->setUniform1f("u_srcLod", static_cast<float>(mip - 1));
            m_reduce->setUniform2i("u_srcSize", hiz.width(mip - 1), hiz.height(mip - 1));
        }

        hiz.attachMip(mip);
        ctx.screenTri.emit();
    }

    hiz.allowAllSampling();
    hiz.unbindFbo();
    endFullscreen(ctx.gl);

    hiz.markBuilt();
}

} // namespace Engine
