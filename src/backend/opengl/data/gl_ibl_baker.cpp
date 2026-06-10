#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_ibl_baker.h"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "logger.h"

#include "gl_context.h"

#include "data/gl_ibl.h"
#include "data/gl_mesh.h"

#include "generator/mesh_generators.h"
#include "loader/environment_loaders.h"

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

GLIBLBaker::GLIBLBaker()
    : m_equirect("shaders/ibl/equirect")
    , m_irradiance("shaders/ibl/irradiance")
    , m_prefilter("shaders/ibl/prefilter")
    , m_brdf("shaders/ibl/brdf")
    , m_cube(std::make_unique<GLMesh>(generateCube()))
{
}

GLIBLBaker::~GLIBLBaker() = default;

void GLIBLBaker::bake(Core::Context& gl, GLIBL& ibl, const std::string& path) {
    HDRImage img = loadHDRImage(path);
    if (!img.valid()) {
        LOG_ERROR("GLIBLBaker: could not load '%s' - IBL stays off", path.c_str());
        return;
    }

    ibl.createTargets();
    ibl.uploadEquirect(img.width, img.height, img.pixels.data());

    const bool prevDepth = gl.isDepthTestEnabled();
    const bool prevCull  = gl.isFaceCullingEnabled();
    gl.setDepthTest(false);
    gl.setFaceCulling(false);

    ibl.bindCaptureFbo();

    const glm::mat4 proj = captureProjection();
    glm::mat4 views[6];
    captureViews(views);

    // 1. Equirectangular HDR -> environment cubemap.
    m_equirect.bind();
    m_equirect.setUniformMatrix4fv("u_projection", proj);
    ibl.bindEquirect(0);
    for (int face = 0; face < 6; ++face) {
        m_equirect.setUniformMatrix4fv("u_view", views[face]);
        ibl.attachEnvFace(face);
        m_cube->draw();
    }
    ibl.generateEnvMips();

    // 2. Diffuse irradiance convolution.
    m_irradiance.bind();
    m_irradiance.setUniformMatrix4fv("u_projection", proj);
    ibl.bindEnvCube(0);
    for (int face = 0; face < 6; ++face) {
        m_irradiance.setUniformMatrix4fv("u_view", views[face]);
        ibl.attachIrradianceFace(face);
        m_cube->draw();
    }

    // 3. GGX prefiltered specular, one roughness per mip.
    m_prefilter.bind();
    m_prefilter.setUniformMatrix4fv("u_projection", proj);
    ibl.bindEnvCube(0);
    for (int mip = 0; mip < GLIBL::PREFILTER_MIPS; ++mip) {
        const float roughness = static_cast<float>(mip) / static_cast<float>(GLIBL::PREFILTER_MIPS - 1);
        m_prefilter.setUniform1f("u_roughness", roughness);
        for (int face = 0; face < 6; ++face) {
            m_prefilter.setUniformMatrix4fv("u_view", views[face]);
            ibl.attachPrefilterFace(face, mip);
            m_cube->draw();
        }
    }

    // 4. Split-sum BRDF/DFG LUT (fullscreen, once per bake).
    m_brdf.bind();
    ibl.attachBrdf();
    m_brdfTri.draw();

    ibl.unbindCaptureFbo();

    gl.setDepthTest(prevDepth);
    gl.setFaceCulling(prevCull);

    ibl.markBaked(path);
    LOG_INFO("IBL baked from '%s'", path.c_str());
}

} // namespace Engine
