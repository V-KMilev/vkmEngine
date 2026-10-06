#include "pass/gl_splash_pass.h"

#include <algorithm>

#include <GL/glew.h>

#include "gl_shader.h"
#include "gl_context.h"
#include "gl_screen_triangle.h"
#include "gl_frame_buffer.h"
#include "gl_texture.h"

#include "gl_frame_context.h"
#include "asset/gl_asset_texture.h"
#include "convention/gl_bindings.h"
#include "debug/engine_error_log.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

// How much of each screen axis the mark may span.
constexpr float LOGO_EXTENT = 0.42f;

} // namespace

GLSplashPass::GLSplashPass()
    : m_shader("shaders/splash") {}

GLSplashPass::~GLSplashPass() = default;

void GLSplashPass::adopt(const std::string& key) {
    m_key = key;
    m_logo.reset();
    m_aspect = 1.0f;

    m_logo = uploadImageFile(key);
    if (!m_logo) {
        // The project named it, so it is the author's to fix.
        reportError("Project", "splash image '" + key + "'", "could not be decoded; its turn shows black");
        return;
    }
    m_aspect = static_cast<float>(m_logo->getWidth()) / static_cast<float>(m_logo->getHeight());
}

void GLSplashPass::execute(GLFrameContext& ctx) {
    const SplashFrame& splash = ctx.view.splash;
    if (!splash.isShowing()) {
        // Nothing needs the logo while nothing is showing; a later key decodes again.
        if (!m_key.empty()) {
            m_key.clear();
            m_logo.reset();
        }
        return;
    }

    if (splash.key != m_key) adopt(splash.key);

    // The whole surface, not the viewport: in the editor the viewport is only a panel.
    Vkm::GL::FrameBuffer::bindDefault();
    ctx.gl.setViewport(
        0,
        0,
        static_cast<int32_t>(ctx.view.surfaceWidth),
        static_cast<int32_t>(ctx.view.surfaceHeight)
    );
    ctx.gl.setDepthTest(false);

    // Fit the mark inside LOGO_EXTENT of each axis, keeping its proportions.
    const float surfaceW = static_cast<float>(std::max(ctx.view.surfaceWidth, 1u));
    const float surfaceH = static_cast<float>(std::max(ctx.view.surfaceHeight, 1u));
    const float screen   = surfaceW / surfaceH;

    float width  = LOGO_EXTENT;
    float height = LOGO_EXTENT * screen / m_aspect;
    if (height > LOGO_EXTENT) {
        height = LOGO_EXTENT;
        width  = LOGO_EXTENT * m_aspect / screen;
    }

    m_shader.bind();
    m_shader.setUniform4f("u_rect", 0.5f - width * 0.5f, 0.5f - height * 0.5f, width, height);
    m_shader.setUniform1f("u_opacity", splash.opacity);
    m_shader.setUniform1i("u_hasLogo", m_logo ? 1 : 0);
    if (m_logo) m_logo->bindSlot(GLBindings::OverlayTextureSlots::SPLASH_LOGO);

    ctx.screenTri.draw();
}

} // namespace Vkm::Engine
