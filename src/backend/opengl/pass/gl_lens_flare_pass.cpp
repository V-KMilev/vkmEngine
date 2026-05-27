#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_lens_flare_pass.h"

#include <GL/glew.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "logger.h"
#include "debug/print_helper.h"
#include "debug/profiler_gl.h"

#include "core/gl_backend.h"
#include "core/gl_scene_target.h"
#include "resource/gl_shader_program.h"

#include "texture/gl_texture.h"
#include "gl_screen_triangle.h"

#include "system/render/render_view.h"
#include "resource/resource_manager.h"

namespace Engine {

namespace {

// Procedural aperture-blade starburst: a radial spoke pattern with a steep
// radial falloff. Multiplied onto the lens-flare halo so the halo reads as
// the diffraction starburst of a real aperture. Single-channel (R8).
std::unique_ptr<Core::Texture2D> makeStarburstTexture() {
    constexpr int  W       = 256;
    constexpr int  H       = 256;
    constexpr int  SPOKES  = 6;          // primary diffraction spokes
    constexpr float FALLOFF = 5.0f;      // higher = tighter, smaller star

    std::vector<uint8_t> px(W * H, 0);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float dx = (x - W * 0.5f) / (W * 0.5f);
            float dy = (y - H * 0.5f) / (H * 0.5f);
            float r  = std::sqrt(dx * dx + dy * dy);
            float th = std::atan2(dy, dx);

            // 6 main spokes (high exponent = narrow) + 12 weaker (2x freq).
            float main = std::pow(std::max(std::cos(th * float(SPOKES)),       0.0f), 24.0f);
            float sub  = std::pow(std::max(std::cos(th * float(SPOKES * 2)),   0.0f), 12.0f) * 0.4f;
            float v    = (main + sub) * std::exp(-r * r * FALLOFF);

            px[y * W + x] = static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
        }
    }

    Core::Texture2DParams p;
    p.width           = W;
    p.height          = H;
    p.internalFormat  = GL_R8;
    p.format          = GL_RED;
    p.type            = GL_UNSIGNED_BYTE;
    p.wrapS           = Core::TextureWrap::ClampToEdge;
    p.wrapT           = Core::TextureWrap::ClampToEdge;
    p.minFilter       = Core::TextureMinFilter::Linear;
    p.magFilter       = Core::TextureMagFilter::Linear;
    p.generateMipmaps = false;
    p.data            = px.data();
    return std::make_unique<Core::Texture2D>("lens_starburst_procedural", p);
}

} // namespace

bool GLLensFlarePass::enabledForView(const RenderView& view) const {
    return isEnabled() && view.environment.lensFlare.enabled && !view.modeConfig.disablePost;
}

GLLensFlarePass::GLLensFlarePass(ShaderHandle shader)
    : RenderPass("GLLensFlarePass")
    , m_shader(shader)
    , m_screenTri(std::make_unique<Core::ScreenTriangle>())
{
}

GLLensFlarePass::~GLLensFlarePass() = default;

void GLLensFlarePass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // Reuses GLSceneTarget, which is owned and resized by GLBackend.
}

void GLLensFlarePass::execute(RenderGraphContext& rg) {
    PROFILE_GPU_SCOPE_NAMED(getName().c_str());
    RenderBackend& backend = rg.backend;
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLLensFlarePass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }
    auto& gl  = static_cast<GLBackend&>(backend);
    auto& hdr = *rg.resource<GLSceneTarget>(RGResource::SceneHDR);
    if (!hdr.isReady()) return;

    GLShader* shader = gl.getView().resolveShader(m_shader, resources);
    if (!shader) return;

    // Same trick as SSR: bind the MSAA HDR target for write, sample the
    // resolved single-sample texture for read. They are distinct GL objects
    // so the feedback loop is avoided. The graph's auto-resolve produced
    // the resolved input before this pass ran.
    hdr.bindForRender();

    auto& ctx = gl.getContext();
    ctx.setDepthTest(false);
    ctx.setDepthWrite(false);
    ctx.setFaceCulling(false);
    ctx.setBlending(true);
    ctx.setBlendFunc(GL_ONE, GL_ONE);   // additive: flare adds onto the scene

    shader->bind();
    shader->setUniform1f("u_threshold",    view.environment.lensFlare.threshold);
    shader->setUniform1f("u_intensity",    view.environment.lensFlare.intensity);
    shader->setUniform1f("u_chromatic",    view.environment.lensFlare.chromatic);
    shader->setUniform1i("u_ghostCount",   view.environment.lensFlare.ghostCount);
    shader->setUniform1f("u_ghostSpacing", view.environment.lensFlare.ghostSpacing);
    shader->setUniform1f("u_haloRadius",   view.environment.lensFlare.haloRadius);

    // Starburst (slot 1): procedural aperture-blade pattern multiplied into
    // the halo, rotated by camera yaw so the spokes feel anchored to the
    // physical lens, not painted on the screen.
    if (view.environment.lensFlare.starburst.enabled && !m_starburst) m_starburst = makeStarburstTexture();
    if (m_starburst) m_starburst->bindSlot(1);
    const bool starOn = view.environment.lensFlare.starburst.enabled && m_starburst;
    shader->setUniform1i("u_starburstEnabled", starOn ? 1 : 0);
    shader->setUniform1f("u_starburstIntensity", view.environment.lensFlare.starburst.intensity);
    // Camera forward in world space (view matrix rows = camera basis in world).
    const float fx = view.camera.view[0][2];
    const float fz = view.camera.view[2][2];
    shader->setUniform1f("u_starburstRotation", std::atan2(fx, fz));

    hdr.bindResolvedColor(0);

    m_screenTri->draw();

    ctx.setBlending(false);
    ctx.setDepthTest(true);
    ctx.setDepthWrite(true);
}

} // namespace Engine
