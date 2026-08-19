#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_ibl_baker.h"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "logger.h"

#include "gl_context.h"

#include "data/gl_cube_convolver.h"
#include "data/gl_ibl.h"
#include "data/gl_mesh.h"

#include "loader/environment_loaders.h"

namespace Vkm::Engine {

GLIBLBaker::GLIBLBaker(GLCubeConvolver& convolver)
    : m_equirect("shaders/ibl/equirect")
    , m_sky("shaders/ibl/sky")
    , m_brdf("shaders/ibl/brdf")
    , m_convolver(convolver)
{
}

GLIBLBaker::~GLIBLBaker() = default;

void GLIBLBaker::captureEnvFaces(Vkm::GL::Context& gl, GLIBL& ibl, Vkm::GL::Shader& shader) {
    // Reuse the convolver's unit cube + 90deg face basis (the env capture shares
    // the convolution projection). The caller has bound @p shader and set its own
    // source uniforms.
    shader.setUniformMatrix4fv("u_projection", m_convolver.projection());
    for (int face = 0; face < 6; ++face) {
        shader.setUniformMatrix4fv("u_view", m_convolver.faceView(face));
        ibl.attachEnvFace(gl, face);
        m_convolver.cube().draw();
    }
    ibl.generateEnvMips();
}

void GLIBLBaker::convolve(Vkm::GL::Context& gl, GLIBL& ibl) {
    // Diffuse irradiance + GGX prefilter, the loops shared with the probe baker.
    m_convolver.irradiance(
        [&] { ibl.bindEnvCube(0); },
        [&](int face) { ibl.attachIrradianceFace(gl, face); });
    m_convolver.prefilter(GLIBL::PREFILTER_MIPS,
        [&] { ibl.bindEnvCube(0); },
        [&](int face, int mip) { ibl.attachPrefilterFace(gl, face, mip); });

    // Split-sum BRDF/DFG LUT (fullscreen, once per bake).
    m_brdf.bind();
    ibl.attachBrdf(gl);
    m_brdfTri.draw();
}

void GLIBLBaker::bake(Vkm::GL::Context& gl, GLIBL& ibl, const std::string& path) {
    HDRImage img = loadHDRImage(path);
    if (!img.isValid()) {
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

    m_equirect.bind();
    ibl.bindEquirect(0);
    captureEnvFaces(gl, ibl, m_equirect);
    convolve(gl, ibl);

    ibl.unbindCaptureFbo();

    gl.setDepthTest(prevDepth);
    gl.setFaceCulling(prevCull);

    ibl.markReady();
    LOG_INFO("IBL baked from '%s'", path.c_str());
}

void GLIBLBaker::bakeProcedural(Vkm::GL::Context& gl, GLIBL& ibl, const SkyParams& sky) {
    ibl.createTargets();

    const bool prevDepth = gl.isDepthTestEnabled();
    const bool prevCull  = gl.isFaceCullingEnabled();
    gl.setDepthTest(false);
    gl.setFaceCulling(false);

    ibl.bindCaptureFbo();

    // Rayleigh + Mie atmosphere -> environment cubemap, then the same convolve +
    // BRDF LUT the HDR path runs, so ambient lighting follows the sky.
    m_sky.bind();
    m_sky.setUniform3fv("u_sunDir",       sky.sunDir);
    m_sky.setUniform1f("u_sunIntensity",  sky.sunIntensity);
    m_sky.setUniform1f("u_rayleigh",      sky.rayleigh);
    m_sky.setUniform1f("u_mie",           sky.mie);
    m_sky.setUniform1f("u_mieG",          sky.mieG);
    m_sky.setUniform3fv("u_nightRadiance", sky.nightRadiance);
    m_sky.setUniform3fv("u_moonDir",       sky.moonDir);
    m_sky.setUniform1f("u_moonHalo",       sky.moonHalo);
    captureEnvFaces(gl, ibl, m_sky);
    convolve(gl, ibl);

    ibl.unbindCaptureFbo();

    gl.setDepthTest(prevDepth);
    gl.setFaceCulling(prevCull);

    ibl.markReady();
    LOG_INFO("IBL baked from procedural sky");
}

} // namespace Vkm::Engine
