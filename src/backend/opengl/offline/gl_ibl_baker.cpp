#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "offline/gl_ibl_baker.h"

#include <algorithm>
#include <cmath>

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "logger.h"

#include "gl_context.h"
#include "gl_texture.h"

#include "convention/gl_bindings.h"
#include "offline/gl_cube_convolver.h"
#include "storage/gl_atmosphere.h"
#include "storage/gl_ibl.h"
#include "asset/gl_mesh.h"

#include "loader/environment_loaders.h"

namespace Vkm::Engine {

namespace {

/// A bake's convolution, from the back env cube into the back targets.
class BackTarget final : public GLCubeConvolver::Target {
    public:
        BackTarget(Vkm::GL::Context& gl, GLIBL& ibl) : m_gl(gl), m_ibl(ibl) {}
        ~BackTarget() override = default;

        BackTarget(const BackTarget& other) = delete;
        BackTarget& operator=(const BackTarget& other) = delete;

        BackTarget(BackTarget && other) = delete;
        BackTarget& operator=(BackTarget && other) = delete;

    public:
        void makeSourceMips() override { m_ibl.generateEnvMips(); }
        void bindSource() override { m_ibl.bindBackEnvCube(GLBindings::BakeTextureSlots::SOURCE); }
        void attachIrradiance(int face) override { m_ibl.attachIrradianceFace(m_gl, face); }
        void attachPrefilter(int face, int mip) override { m_ibl.attachPrefilterFace(m_gl, face, mip); }

    private:
        Vkm::GL::Context& m_gl;
        GLIBL&            m_ibl;
};

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

// A bake runs in whatever state the last draw left, so it states what it relies on: blending left
// on would accumulate into the faces. Nothing is restored; GLBackend::render resets it per pass.
void bakeState(Vkm::GL::Context& gl) {
    gl.setDepthTest(false);
    gl.setFaceCulling(false);
    gl.setBlending(false);
}

} // namespace

int GLIBLBaker::stepsFor(const SkyParams& asked, const SkyParams& shown) {
    const auto degrees = [](const glm::vec3& a, const glm::vec3& b) {
        const float cosine = glm::dot(glm::normalize(a), glm::normalize(b));
        return glm::degrees(std::acos(glm::clamp(cosine, -1.0f, 1.0f)));
    };
    const float behind = std::max(degrees(asked.sunDir, shown.sunDir), degrees(asked.moonDir, shown.moonDir));
    return std::clamp(1 + static_cast<int>(behind / CATCH_UP_DEGREES), 1, MAX_CATCH_UP_STEPS);
}

GLIBLBaker::GLIBLBaker(GLCubeConvolver& convolver)
    : m_equirect("shaders/ibl/equirect")
    , m_sky("shaders/ibl/sky")
    , m_transmittance("shaders/atmosphere/transmittance")
    , m_multiScattering("shaders/atmosphere/multiscatter")
    , m_brdf("shaders/ibl/brdf")
    , m_convolver(convolver)
    , m_steps(GLCubeConvolver::slices(GLIBL::PREFILTER_SIZE, GLIBL::PREFILTER_MIPS))
    , m_next(idle()) {}

GLIBLBaker::~GLIBLBaker() = default;

void GLIBLBaker::captureFace(Vkm::GL::Context& gl, GLIBL& ibl, Vkm::GL::Shader& source, int face) {
    // The env capture shares the convolver's unit cube and 90deg face projection.
    source.setUniformMatrix4fv("u_projection", m_convolver.projection());
    source.setUniformMatrix4fv("u_view", m_convolver.faceView(face));
    ibl.attachEnvFace(gl, face);
    m_convolver.cube().draw();
}

void GLIBLBaker::integrateBrdf(Vkm::GL::Context& gl, GLIBL& ibl) {
    ibl.createBrdf();
    bakeState(gl);

    ibl.bindCaptureFbo();
    m_brdf.bind();
    ibl.attachBrdf(gl);
    m_lutTri.draw();
    ibl.unbindCaptureFbo();
}

void GLIBLBaker::bakeAir(
    Vkm::GL::Context& gl,
    GLAtmosphere& atmosphere,
    const Atmosphere::Coefficients& air
) {
    atmosphere.createAir();
    bakeState(gl);

    atmosphere.bindFbo();
    m_transmittance.bind();
    GLAtmosphere::setExtinction(m_transmittance, air);
    atmosphere.attachTransmittance(gl);
    m_lutTri.draw();

    m_multiScattering.bind();
    GLAtmosphere::setAir(m_multiScattering, air);
    atmosphere.bindTransmittance(GLBindings::BakeTextureSlots::TRANSMITTANCE);
    atmosphere.attachMultiScattering(gl);
    m_lutTri.draw();
    atmosphere.unbindFbo();
}

void GLIBLBaker::bakeFrom(Vkm::GL::Context& gl, GLIBL& ibl, Vkm::GL::Shader& source) {
    ibl.createTargets();
    bakeState(gl);

    ibl.bindCaptureFbo();
    for (int face = 0; face < 6; ++face) captureFace(gl, ibl, source, face);
    ibl.generateEnvMips();

    ibl.bindBackEnvCube(GLBindings::BakeTextureSlots::SOURCE);
    m_convolver.irradiance([&](int face) { ibl.attachIrradianceFace(gl, face); });
    m_convolver.prefilter(
        GLIBL::PREFILTER_MIPS,
        [&](int face, int mip) { ibl.attachPrefilterFace(gl, face, mip); }
    );
    ibl.unbindCaptureFbo();

    ibl.swap();
}

void GLIBLBaker::bake(Vkm::GL::Context& gl, GLIBL& ibl, const std::string& path) {
    m_next = idle();

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

void GLIBLBaker::bindSky(const GLAtmosphere& atmosphere, const SkyParams& sky) {
    m_sky.bind();
    GLAtmosphere::setSky(m_sky, sky, sky.sunIlluminance);
    m_sky.setUniform3fv("u_sunDir",        sky.sunDir);
    m_sky.setUniform3fv("u_nightRadiance", sky.nightRadiance);
    m_sky.setUniform3fv("u_moonDir",       sky.moonDir);
    m_sky.setUniform1f("u_moonHalo",       sky.moonHalo);
    atmosphere.bindTransmittance(GLBindings::BakeTextureSlots::TRANSMITTANCE);
    atmosphere.bindMultiScattering(GLBindings::BakeTextureSlots::MULTISCATTERING);
}

void GLIBLBaker::bakeProcedural(
    Vkm::GL::Context& gl,
    GLIBL& ibl,
    const GLAtmosphere& atmosphere,
    const SkyParams& sky
) {
    m_next   = idle();
    m_target = sky;

    // The atmosphere into the env cube, then the HDR path's convolve, so ambient follows the sky.
    bindSky(atmosphere, sky);
    bakeFrom(gl, ibl, m_sky);

    LOG_DEBUG("IBL baked from procedural sky");
}

void GLIBLBaker::beginProcedural(const SkyParams& sky) {
    m_next   = 0;
    m_target = sky;
}

bool GLIBLBaker::advance(Vkm::GL::Context& gl, GLIBL& ibl, const GLAtmosphere& atmosphere) {
    if (!baking()) return false;

    const int step = m_next++;
    ibl.createTargets();
    bakeState(gl);
    ibl.bindCaptureFbo();
    if (step == 0) {
        bindSky(atmosphere, m_target);
        for (int face = 0; face < 6; ++face) captureFace(gl, ibl, m_sky, face);
    } else {
        BackTarget target(gl, ibl);
        m_convolver.run(gl, m_steps[static_cast<size_t>(step - 1)], target);
    }
    ibl.unbindCaptureFbo();

    if (baking()) return false;
    ibl.swap();
    return true;
}

} // namespace Vkm::Engine
