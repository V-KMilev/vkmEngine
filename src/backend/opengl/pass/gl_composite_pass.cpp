#include "gl_composite_pass.h"

#include <GL/glew.h>

#include "logger.h"
#include "debug/print_helper.h"
#include "debug/statistics.h"

#include "core/gl_backend.h"
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

    // Composite to the backbuffer (full window).
    backend.getDefaultTarget().bind();

    // Fullscreen blit: no depth, no blend, no culling.
    glContext.setDepthTest(false);
    glContext.setDepthWrite(false);
    glContext.setBlending(false);
    glContext.setFaceCulling(false);

    shader->bind();
    STATS_RECORD_SHADER_SWITCH();

    // Display transform selector (AgX / PBR Neutral / ACES / Reinhard).
    shader->setUniform1i("u_tonemap", view.environment.tonemap);

    // CameraBlock UBO (binding 2) is bound for the frame by GLView; the
    // composite shader reads exposure from cameraPosition.w.
    hdr.bindResolvedColor(0);

    // Bloom (slot 1). Strength 0 when unavailable so it is a no-op.
    auto& bloom = *rg.resource<GLBloom>(RGResource::BloomChain);
    const bool bloomReady = bloom.isReady();
    bloom.bind(1);
    shader->setUniform1f("u_bloomStrength",
        bloomReady ? view.environment.bloomStrength : 0.0f);

    // Auto-exposure (slot 2). Off when unavailable -> manual exposure only.
    auto& ae = *rg.resource<GLAutoExposure>(RGResource::AdaptedLuminance);
    const bool aeOn = ae.isReady() && view.environment.autoExposure;
    ae.bindAdapted(2);
    shader->setUniform1i("u_autoExposure", aeOn ? 1 : 0);
    shader->setUniform1f("u_exposureKey", view.environment.exposureKey);
    shader->setUniform1f("u_exposureMin", view.environment.exposureMin);
    shader->setUniform1f("u_exposureMax", view.environment.exposureMax);

    // Color-grading LUT (slot 3). Loaded lazily when the path changes; a
    // failed path is recorded so it is not retried every frame.
    const std::string& lutPath = view.environment.colorLutPath;
    if (view.environment.colorGrade && !lutPath.empty() && lutPath != m_lutPath) {
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
    const bool lutOn = view.environment.colorGrade && m_lut
        && !lutPath.empty() && lutPath == m_lutPath;
    // Slot 3: bind when present; the u_lutEnabled gate decides actual use.
    if (m_lut) m_lut->bindSlot(3);
    shader->setUniform1i("u_lutEnabled", lutOn ? 1 : 0);
    shader->setUniform1f("u_lutIntensity", view.environment.colorGradeIntensity);

    m_screenTri->draw();

    // Restore depth state for the next frame's scene passes (they rely on
    // the context default being enabled rather than setting it themselves).
    glContext.setDepthTest(true);
    glContext.setDepthWrite(true);
}

} // namespace Engine
