#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "offline/gl_ibl_baker.h"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "logger.h"

#include "gl_context.h"
#include "gl_texture.h"

#include "convention/gl_bindings.h"
#include "offline/gl_cube_convolver.h"
#include "storage/gl_ibl.h"
#include "asset/gl_mesh.h"

#include "loader/environment_loaders.h"

namespace Vkm::Engine {

namespace {

// Half floats with levels, since a source wider than the faces is minified and would alias;
// wrapped in longitude, where the image's edges meet behind -X.
Vkm::GL::Texture2DParams equirectParams(const HDRImage& image) {
    Vkm::GL::Texture2DParams params;
    params.width           = image.width;
    params.height          = image.height;
    params.internalFormat  = GL_RGB16F;
    params.format          = GL_RGB;
    params.type            = GL_FLOAT;
    params.wrapS           = Vkm::GL::TextureWrap::Repeat;
    params.wrapT           = Vkm::GL::TextureWrap::ClampToEdge;
    params.minFilter       = Vkm::GL::TextureMinFilter::LinearMipmapLinear;
    params.magFilter       = Vkm::GL::TextureMagFilter::Linear;
    params.generateMipmaps = true;
    params.data            = image.pixels.data();
    return params;
}

} // namespace

GLIBLBaker::GLIBLBaker(GLCubeConvolver& convolver)
    : m_equirect("shaders/ibl/equirect")
    , m_sky("shaders/ibl/sky")
    , m_brdf("shaders/ibl/brdf")
    , m_convolver(convolver) {}

GLIBLBaker::~GLIBLBaker() = default;

void GLIBLBaker::captureEnvFaces(Vkm::GL::Context& gl, GLIBL& ibl, Vkm::GL::Shader& shader) {
    // The env capture shares the convolver's unit cube and 90deg face projection.
    shader.setUniformMatrix4fv("u_projection", m_convolver.projection());
    for (int face = 0; face < 6; ++face) {
        shader.setUniformMatrix4fv("u_view", m_convolver.faceView(face));
        ibl.attachEnvFace(gl, face);
        m_convolver.cube().draw();
    }
    ibl.generateEnvMips();
}

void GLIBLBaker::convolve(Vkm::GL::Context& gl, GLIBL& ibl) {
    ibl.bindEnvCube(GLBindings::BakeTextureSlots::SOURCE);
    m_convolver.irradiance([&](int face) { ibl.attachIrradianceFace(gl, face); });
    m_convolver.prefilter(
        GLIBL::PREFILTER_MIPS,
        [&](int face, int mip) { ibl.attachPrefilterFace(gl, face, mip); }
    );
}

void GLIBLBaker::integrateBrdf(Vkm::GL::Context& gl, GLIBL& ibl) {
    ibl.createBrdf();

    gl.setDepthTest(false);
    gl.setFaceCulling(false);
    gl.setBlending(false);

    ibl.bindCaptureFbo();
    m_brdf.bind();
    ibl.attachBrdf(gl);
    m_brdfTri.draw();
    ibl.unbindCaptureFbo();
}

void GLIBLBaker::bakeFrom(Vkm::GL::Context& gl, GLIBL& ibl, Vkm::GL::Shader& source) {
    ibl.createTargets();

    // A bake runs in whatever state the last draw left, so it states all three: blending left on
    // would accumulate into the faces. Nothing is restored; GLBackend::render resets it per pass.
    gl.setDepthTest(false);
    gl.setFaceCulling(false);
    gl.setBlending(false);

    ibl.bindCaptureFbo();
    captureEnvFaces(gl, ibl, source);
    convolve(gl, ibl);
    ibl.unbindCaptureFbo();

    ibl.markReady();
}

void GLIBLBaker::bake(Vkm::GL::Context& gl, GLIBL& ibl, const std::string& path) {
    // Whatever the cubes held was another sky's, so a load that fails leaves none.
    ibl.markUnready();

    HDRImage img = loadHDRImage(path);
    if (!img.isValid()) {
        LOG_ERROR("GLIBLBaker: could not load '%s' - IBL stays off", path.c_str());
        return;
    }

    // Read by nothing after the capture, so it goes with the bake rather than staying resident.
    const Vkm::GL::Texture2D equirect("ibl_equirect", equirectParams(img));
    equirect.bindSlot(GLBindings::BakeTextureSlots::SOURCE);

    m_equirect.bind();
    bakeFrom(gl, ibl, m_equirect);

    LOG_INFO("IBL baked from '%s'", path.c_str());
}

void GLIBLBaker::bakeProcedural(Vkm::GL::Context& gl, GLIBL& ibl, const SkyParams& sky) {
    // Rayleigh + Mie atmosphere into the env cube, then the HDR path's convolve, so ambient
    // follows the sky.
    m_sky.bind();
    m_sky.setUniform3fv("u_sunDir",        sky.sunDir);
    m_sky.setUniform1f("u_sunIntensity",   sky.sunIntensity);
    m_sky.setUniform3fv("u_rayleighScattering", sky.air.rayleighScattering);
    m_sky.setUniform3fv("u_mieScattering",      sky.air.mieScattering);
    m_sky.setUniform3fv("u_mieExtinction",      sky.air.mieExtinction);
    m_sky.setUniform1f("u_mieG",           sky.mieG);
    m_sky.setUniform3fv("u_nightRadiance", sky.nightRadiance);
    m_sky.setUniform3fv("u_moonDir",       sky.moonDir);
    m_sky.setUniform1f("u_moonHalo",       sky.moonHalo);
    bakeFrom(gl, ibl, m_sky);

    LOG_DEBUG("IBL baked from procedural sky");
}

} // namespace Vkm::Engine
