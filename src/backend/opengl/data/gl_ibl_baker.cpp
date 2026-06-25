#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_ibl_baker.h"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "logger.h"

#include "gl_context.h"

#include "data/gl_ibl.h"
#include "data/gl_mesh.h"

#include "loader/environment_loaders.h"

namespace Engine {

GLIBLBaker::GLIBLBaker()
    : m_equirect("shaders/ibl/equirect")
    , m_brdf("shaders/ibl/brdf")
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

    // 1. Equirectangular HDR -> environment cubemap. Reuse the convolver's unit
    // cube + face basis (the env capture shares the 90deg convolution projection).
    m_equirect.bind();
    m_equirect.setUniformMatrix4fv("u_projection", m_convolver.projection());
    ibl.bindEquirect(0);
    for (int face = 0; face < 6; ++face) {
        m_equirect.setUniformMatrix4fv("u_view", m_convolver.faceView(face));
        ibl.attachEnvFace(gl, face);
        m_convolver.cube().draw();
    }
    ibl.generateEnvMips();

    // 2. Diffuse irradiance + 3. GGX prefilter, the loops shared with the probe baker.
    m_convolver.irradiance(
        [&] { ibl.bindEnvCube(0); },
        [&](int face) { ibl.attachIrradianceFace(gl, face); });
    m_convolver.prefilter(GLIBL::PREFILTER_MIPS,
        [&] { ibl.bindEnvCube(0); },
        [&](int face, int mip) { ibl.attachPrefilterFace(gl, face, mip); });

    // 4. Split-sum BRDF/DFG LUT (fullscreen, once per bake).
    m_brdf.bind();
    ibl.attachBrdf(gl);
    m_brdfTri.draw();

    ibl.unbindCaptureFbo();

    gl.setDepthTest(prevDepth);
    gl.setFaceCulling(prevCull);

    ibl.markReady();
    LOG_INFO("IBL baked from '%s'", path.c_str());
}

} // namespace Engine
