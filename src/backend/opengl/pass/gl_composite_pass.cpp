#include "gl_composite_pass.h"

#include <GL/glew.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "logger.h"
#include "debug/print_helper.h"
#include "debug/statistics.h"

#include "core/gl_backend.h"
#include "core/gl_hdr_target.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_bloom.h"
#include "resource/gl_auto_exposure.h"

#include "gl_screen_triangle.h"
#include "texture/gl_texture.h"

#include "system/render/render_view.h"
#include "system/render/render_target.h"
#include "resource/resource_manager.h"
#include "loader/environment_loaders.h"

namespace Engine {

namespace {

// Procedural lens-dirt mask: sparse Gaussian "dust" blobs over a low-noise
// background, plus a few faint diagonal streaks. Deterministic. Tileable
// because every blob wraps on torus distance, so sampling with plain
// non-clamped UVs would not show seams either.
std::unique_ptr<Core::Texture2D> makeDirtTexture() {
    constexpr int W = 512;
    constexpr int H = 512;

    auto hash = [](uint32_t s) {
        s = (s ^ 61u) ^ (s >> 16);
        s *= 9u;
        s = s ^ (s >> 4);
        s *= 0x27d4eb2du;
        s = s ^ (s >> 15);
        return (s & 0xFFFFFFu) / float(0xFFFFFFu);
    };

    std::vector<uint8_t> pixels(W * H * 4, 0);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float n = hash(static_cast<uint32_t>(y * W + x));
            float v = 0.02f + 0.03f * n;          // dim background grit
            pixels[(y * W + x) * 4 + 0] = static_cast<uint8_t>(v * 255.0f);
            pixels[(y * W + x) * 4 + 1] = static_cast<uint8_t>(v * 255.0f);
            pixels[(y * W + x) * 4 + 2] = static_cast<uint8_t>(v * 255.0f);
            pixels[(y * W + x) * 4 + 3] = 255;
        }
    }

    // ~120 dust blobs of varying sizes / brightness, scattered uniformly.
    constexpr int BLOBS = 120;
    for (int b = 0; b < BLOBS; ++b) {
        float cx = hash(static_cast<uint32_t>(b * 3u + 0u)) * W;
        float cy = hash(static_cast<uint32_t>(b * 3u + 1u)) * H;
        float rs = hash(static_cast<uint32_t>(b * 3u + 2u));
        float radius = 2.0f + rs * 10.0f;
        float bright = 0.35f + rs * 0.55f;
        int r2 = static_cast<int>(radius * 3.0f);

        for (int dy = -r2; dy <= r2; ++dy) {
            for (int dx = -r2; dx <= r2; ++dx) {
                int xi = (static_cast<int>(cx) + dx + W) % W;
                int yi = (static_cast<int>(cy) + dy + H) % H;
                float d2 = float(dx * dx + dy * dy);
                float g = std::exp(-d2 / (radius * radius)) * bright;
                int idx = (yi * W + xi) * 4;
                for (int c = 0; c < 3; ++c) {
                    int v = pixels[idx + c] + static_cast<int>(g * 255.0f);
                    pixels[idx + c] = static_cast<uint8_t>(std::min(v, 255));
                }
            }
        }
    }

    Core::Texture2DParams p;
    p.width           = W;
    p.height          = H;
    p.internalFormat  = GL_RGBA8;
    p.format          = GL_RGBA;
    p.type            = GL_UNSIGNED_BYTE;
    p.wrapS           = Core::TextureWrap::Repeat;
    p.wrapT           = Core::TextureWrap::Repeat;
    p.minFilter       = Core::TextureMinFilter::Linear;
    p.magFilter       = Core::TextureMagFilter::Linear;
    p.generateMipmaps = false;
    p.data            = pixels.data();
    return std::make_unique<Core::Texture2D>("lens_dirt_procedural", p);
}

} // namespace

GLCompositePass::GLCompositePass(ShaderHandle shader)
    : RenderPass("GLCompositePass")
    , m_shader(shader)
    , m_screenTri(std::make_unique<Core::ScreenTriangle>())
{
}

GLCompositePass::~GLCompositePass() = default;

void GLCompositePass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // The HDR target is owned and resized by GLBackend; nothing to do here.
}

void GLCompositePass::execute(RenderGraphContext& rg) {
    RenderBackend& backend = rg.backend;
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLCompositePass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }

    auto& gl  = static_cast<GLBackend&>(backend);
    auto& hdr = *rg.resource<GLHdrTarget>(RGResource::SceneHDR);
    if (!hdr.isReady()) return;

    GLShader* shader = gl.getView().resolveShader(m_shader, resources);
    if (!shader) return;

    auto& glContext = gl.getContext();

    // SceneHDRResolved is produced by the graph (auto MSAA-resolve) before
    // this pass runs - see RenderGraph::execute.

    // Composite to the backbuffer. The graph routes RGResource::Backbuffer
    // at either the window backbuffer or the offscreen preview target
    // depending on whether a preview session is active; this is what makes
    // the unchanged graph render into either destination cleanly.
    auto* backbuffer = rg.resource<RenderTarget>(RGResource::Backbuffer);
    if (backbuffer) backbuffer->bind();
    else backend.getDefaultTarget().bind();

    // Fullscreen blit: no depth, no blend, no culling.
    glContext.setDepthTest(false);
    glContext.setDepthWrite(false);
    glContext.setBlending(false);
    glContext.setFaceCulling(false);

    shader->bind();
    STATS_RECORD_SHADER_SWITCH();

    // Display transform selector (AgX / PBR Neutral / ACES / Reinhard).
    shader->setUniform1i("u_tonemap", view.environment.tonemap);
    // Wireframe is a diagnostic mode: the shader will skip exposure +
    // tonemap + LUT and go straight to sRGB so unlit lines stay clean.
    shader->setUniform1i("u_wireframe", view.environment.wireframe ? 1 : 0);

    // CameraBlock UBO (binding 2) is bound for the frame by GLView; the
    // composite shader reads exposure from cameraPosition.w.
    hdr.bindResolvedColor(0);

    // Bloom (slot 1). Strength 0 when unavailable so it is a no-op.
    auto& bloom = *rg.resource<GLBloom>(RGResource::BloomChain);
    const bool bloomReady = bloom.isReady();
    bloom.bind(1);
    shader->setUniform1f("u_bloomStrength",
        bloomReady ? view.environment.bloom.strength : 0.0f);

    // Auto-exposure (slot 2). Off when unavailable -> manual exposure only.
    auto& ae = *rg.resource<GLAutoExposure>(RGResource::AdaptedLuminance);
    const bool aeOn = ae.isReady() && view.environment.exposure.autoExposure;
    ae.bindAdapted(2);
    shader->setUniform1i("u_autoExposure", aeOn ? 1 : 0);
    shader->setUniform1f("u_exposureKey", view.environment.exposure.key);
    shader->setUniform1f("u_exposureMin", view.environment.exposure.min);
    shader->setUniform1f("u_exposureMax", view.environment.exposure.max);

    // Color-grading LUT (slot 3). Loaded lazily when the path changes; a
    // failed path is recorded so it is not retried every frame.
    const std::string& lutPath = view.environment.colorGrade.lutPath;
    if (view.environment.colorGrade.enabled && !lutPath.empty() && lutPath != m_lutPath) {
        LDRImage img = loadColorLUT(lutPath);
        if (img.valid()) {
            Core::Texture2DParams p;
            p.width           = static_cast<uint32_t>(img.width);
            p.height          = static_cast<uint32_t>(img.height);
            p.internalFormat  = GL_RGBA8;
            p.format          = GL_RGBA;
            p.type            = GL_UNSIGNED_BYTE;
            p.wrapS           = Core::TextureWrap::ClampToEdge;
            p.wrapT           = Core::TextureWrap::ClampToEdge;
            p.minFilter       = Core::TextureMinFilter::Linear;
            p.magFilter       = Core::TextureMagFilter::Linear;
            p.generateMipmaps = false;
            p.data            = img.pixels.data();
            m_lut = std::make_unique<Core::Texture2D>("color_grade_lut", p);
        }
        m_lutPath = lutPath;  // record (even on failure) to stop per-frame reloads
    }
    const bool lutOn = view.environment.colorGrade.enabled && m_lut
        && !lutPath.empty() && lutPath == m_lutPath;
    // Slot 3: bind when present; the u_lutEnabled gate decides actual use.
    if (m_lut) m_lut->bindSlot(3);
    shader->setUniform1i("u_lutEnabled", lutOn ? 1 : 0);
    shader->setUniform1f("u_lutIntensity", view.environment.colorGrade.intensity);

    // Diagnostic overlay (slot 5): AABB/Grid (and any future debug pass)
    // write to the HDR FBO's overlay attachment instead of HDR colour, so
    // their pixels skip the tonemap chain. Resolve the MSAA overlay to its
    // single-sample texture and bind it here; the shader blends it on top
    // of the tonemapped result based on overlay alpha.
    hdr.resolveOverlay();
    hdr.bindResolvedOverlay(5);
    // Restore previous bindings disturbed by the overlay resolve blits.
    hdr.bindResolvedColor(0);
    if (backbuffer) backbuffer->bind();
    else backend.getDefaultTarget().bind();

    // Lens dirt (slot 4): procedurally generated on first enable, then kept
    // resident. Off when env.lensDirt.enabled is false; the shader gates the multiply.
    if (view.environment.lensDirt.enabled && !m_dirt) m_dirt = makeDirtTexture();
    if (m_dirt) m_dirt->bindSlot(4);
    shader->setUniform1i("u_dirtEnabled",
        (view.environment.lensDirt.enabled && m_dirt) ? 1 : 0);
    shader->setUniform1f("u_dirtIntensity", view.environment.lensDirt.intensity);

    m_screenTri->draw();

    // Restore depth state for the next frame's scene passes (they rely on
    // the context default being enabled rather than setting it themselves).
    glContext.setDepthTest(true);
    glContext.setDepthWrite(true);
}

} // namespace Engine
