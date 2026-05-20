#include "gl_ibl_bake_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "logger.h"
#include "debug/print_helper.h"

#include "core/gl_backend.h"
#include "resource/gl_shader_program.h"
#include "resource/gl_mesh.h"
#include "resource/gl_ibl.h"

#include "gl_screen_triangle.h"

#include "generator/mesh_generators.h"
#include "loader/environment_loaders.h"

#include "system/render/render_view.h"
#include "resource/resource_manager.h"

namespace Engine {

namespace {
    glm::mat4 captureProjection() {
        return glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    }

    void captureViews(glm::mat4 out[6]) {
        const glm::vec3 o(0.0f);
        out[0] = glm::lookAt(o, glm::vec3( 1, 0, 0), glm::vec3(0, -1,  0));
        out[1] = glm::lookAt(o, glm::vec3(-1, 0, 0), glm::vec3(0, -1,  0));
        out[2] = glm::lookAt(o, glm::vec3( 0, 1, 0), glm::vec3(0,  0,  1));
        out[3] = glm::lookAt(o, glm::vec3( 0,-1, 0), glm::vec3(0,  0, -1));
        out[4] = glm::lookAt(o, glm::vec3( 0, 0, 1), glm::vec3(0, -1,  0));
        out[5] = glm::lookAt(o, glm::vec3( 0, 0,-1), glm::vec3(0, -1,  0));
    }
}

GLIBLBakePass::GLIBLBakePass(
    ShaderHandle equirectShader,
    ShaderHandle irradianceShader,
    ShaderHandle prefilterShader,
    ShaderHandle brdfShader
)
    : RenderPass("GLIBLBakePass")
    , m_equirectShader(equirectShader)
    , m_irradianceShader(irradianceShader)
    , m_prefilterShader(prefilterShader)
    , m_brdfShader(brdfShader)
    , m_cube(std::make_unique<GLMesh>(generateCube()))
    , m_brdfScreenTri(std::make_unique<Core::ScreenTriangle>())
{
}

GLIBLBakePass::~GLIBLBakePass() = default;

void GLIBLBakePass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // IBL targets are fixed-size and independent of the window.
}

void GLIBLBakePass::execute(RenderGraphContext& rg) {
    RenderBackend& backend = rg.backend;
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLIBLBakePass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }

    auto& gl     = static_cast<GLBackend&>(backend);
    auto& glView = gl.getView();
    auto& ibl    = *rg.resource<GLIBL>(RGResource::IBL);

    const std::string& path = view.environment.environmentMapPath;
    if (path.empty() || path == m_skipPath) return;
    if (!ibl.needsBake(path)) return;

    HDRImage img = loadHDRImage(path);
    if (!img.valid()) {
        LOG_ERROR("GLIBLBakePass: could not load '%s' - IBL stays off", path.c_str());
        m_skipPath = path;
        return;
    }

    GLShader* eq  = glView.resolveShader(m_equirectShader,   resources);
    GLShader* irr = glView.resolveShader(m_irradianceShader, resources);
    GLShader* pf  = glView.resolveShader(m_prefilterShader,  resources);
    GLShader* br  = glView.resolveShader(m_brdfShader,       resources);
    if (!eq || !irr || !pf || !br) {
        LOG_ERROR("GLIBLBakePass: an IBL bake shader failed to resolve - skipping");
        return;
    }

    ibl.createTargets();
    ibl.uploadEquirect(img.width, img.height, img.pixels.data());

    auto&        ctx        = gl.getContext();
    const bool   prevDepth  = ctx.isDepthTestEnabled();
    const bool   prevCull   = ctx.isFaceCullingEnabled();
    const GLenum prevDFunc  = ctx.getDepthFunc();
    ctx.setDepthTest(false);
    ctx.setFaceCulling(false);

    ibl.bindCaptureFbo();

    const glm::mat4 proj = captureProjection();
    glm::mat4 views[6];
    captureViews(views);

    // 1. Equirectangular HDR -> environment cubemap.
    eq->bind();
    eq->setUniformMatrix4fv("u_projection", proj);
    ibl.bindEquirect(0);
    for (int face = 0; face < 6; ++face) {
        eq->setUniformMatrix4fv("u_view", views[face]);
        ibl.attachEnvFace(face);
        m_cube->draw(GL_TRIANGLES);
    }
    ibl.generateEnvMips();

    // 2. Diffuse irradiance convolution.
    irr->bind();
    irr->setUniformMatrix4fv("u_projection", proj);
    ibl.bindEnvCube(0);
    for (int face = 0; face < 6; ++face) {
        irr->setUniformMatrix4fv("u_view", views[face]);
        ibl.attachIrradianceFace(face);
        m_cube->draw(GL_TRIANGLES);
    }

    // 3. GGX prefiltered specular, one roughness per mip.
    pf->bind();
    pf->setUniformMatrix4fv("u_projection", proj);
    ibl.bindEnvCube(0);
    for (int mip = 0; mip < GLIBL::PREFILTER_MIPS; ++mip) {
        const float roughness = static_cast<float>(mip) / static_cast<float>(GLIBL::PREFILTER_MIPS - 1);
        pf->setUniform1f("u_roughness", roughness);
        for (int face = 0; face < 6; ++face) {
            pf->setUniformMatrix4fv("u_view", views[face]);
            ibl.attachPrefilterFace(face, mip);
            m_cube->draw(GL_TRIANGLES);
        }
    }

    // 4. Split-sum BRDF/DFG LUT (fullscreen, once per bake).
    br->bind();
    ibl.attachBrdf();
    m_brdfScreenTri->draw();

    ibl.unbindCaptureFbo();

    ctx.setDepthTest(prevDepth);
    ctx.setFaceCulling(prevCull);
    ctx.setDepthFunc(prevDFunc);
    ctx.setViewport(0, 0,
        static_cast<int32_t>(view.viewportWidth),
        static_cast<int32_t>(view.viewportHeight));

    ibl.markBaked(path);
    LOG_INFO("IBL baked from '%s'", path.c_str());
}

} // namespace Engine
