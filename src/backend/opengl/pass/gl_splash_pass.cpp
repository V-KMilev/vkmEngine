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
// The caption's line: its height, its widest and its gap to the screen's foot, as shares of the screen.
constexpr float CAPTION_HEIGHT = 0.02f;
constexpr float CAPTION_WIDEST = 0.9f;
constexpr float CAPTION_FOOT   = 0.04f;

} // namespace

GLSplashPass::GLSplashPass()
    : m_shader("shaders/splash") {}

GLSplashPass::~GLSplashPass() = default;

void GLSplashPass::adopt(SplashImage& image, const std::string& key) {
    image.key = key;
    image.texture.reset();
    image.aspect = 1.0f;
    if (key.empty()) return;

    image.texture = uploadImageFile(key);
    if (!image.texture) {
        // A project's splash entry is its author's to fix; the engine's own images ship with it.
        reportError("Splash", "splash image '" + key + "'", "could not be decoded; it is not drawn");
        return;
    }
    const Vkm::GL::Texture2D& decoded = *image.texture;
    image.aspect = static_cast<float>(decoded.getWidth()) / static_cast<float>(decoded.getHeight());
}

void GLSplashPass::execute(GLFrameContext& ctx) {
    const SplashFrame& splash = ctx.view.splash;
    if (!splash.isShowing()) {
        // Nothing needs the images while nothing is showing; a later key decodes again.
        if (!m_logo.key.empty()) adopt(m_logo, {});
        if (!m_caption.key.empty()) adopt(m_caption, {});
        return;
    }

    if (splash.key != m_logo.key) adopt(m_logo, splash.key);
    if (splash.caption != m_caption.key) adopt(m_caption, splash.caption);

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
    float height = LOGO_EXTENT * screen / m_logo.aspect;
    if (height > LOGO_EXTENT) {
        height = LOGO_EXTENT;
        width  = LOGO_EXTENT * m_logo.aspect / screen;
    }

    m_shader.bind();
    m_shader.setUniform4f("u_rect", 0.5f - width * 0.5f, 0.5f - height * 0.5f, width, height);
    m_shader.setUniform1f("u_opacity", splash.opacity);
    m_shader.setUniform1i("u_hasLogo", m_logo.texture ? 1 : 0);
    if (m_logo.texture) m_logo.texture->bindSlot(GLBindings::OverlayTextureSlots::SPLASH_LOGO);

    // The caption's line, centred at the foot, as high as CAPTION_HEIGHT unless that is too wide.
    const float lineW = std::min(CAPTION_HEIGHT * m_caption.aspect / screen, CAPTION_WIDEST);
    const float lineH = lineW * screen / m_caption.aspect;
    m_shader.setUniform4f("u_captionRect", 0.5f - lineW * 0.5f, CAPTION_FOOT, lineW, lineH);
    m_shader.setUniform1i("u_hasCaption", m_caption.texture ? 1 : 0);
    if (m_caption.texture) m_caption.texture->bindSlot(GLBindings::OverlayTextureSlots::SPLASH_CAPTION);

    ctx.screenTri.draw();
}

} // namespace Vkm::Engine
