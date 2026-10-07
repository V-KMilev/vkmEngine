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
#include "storage/gl_atmosphere.h"
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

// A bake runs in whatever state the last draw left, so it states what it relies on: blending left
// on would accumulate into the faces. Nothing is restored; GLBackend::render resets it per pass.
void bakeState(Vkm::GL::Context& gl) {
    gl.setDepthTest(false);
    gl.setFaceCulling(false);
    gl.setBlending(false);
}

} // namespace

GLIBLBaker::GLIBLBaker(GLCubeConvolver& convolver)
    : m_equirect("shaders/ibl/equirect")
    , m_sky("shaders/ibl/sky")
    , m_transmittance("shaders/atmosphere/transmittance")
    , m_multiScattering("shaders/atmosphere/multiscatter")
    , m_brdf("shaders/ibl/brdf")
    , m_convolver(convolver)
{
    // The env cube first, as everything after reads its mips.
    using Kind = Step::Kind;
    m_steps.push_back({Kind::Capture, 0, 0, 0, 1});
    m_steps.push_back({Kind::Mips, 0, 0, 0, 1});
    for (int face = 0; face < 6; ++face) {
        for (int slice = 0; slice < IRRADIANCE_SLICES; ++slice) {
            m_steps.push_back({Kind::Irradiance, face, 0, slice, IRRADIANCE_SLICES});
        }
    }
    for (int face = 0; face < 6; ++face) {
        for (int mip = 1; mip < GLIBL::PREFILTER_MIPS; ++mip) {
            const int bands = prefilterBands(mip);
            for (int band = 0; band < bands; ++band) {
                m_steps.push_back({Kind::Prefilter, face, mip, band, bands});
            }
        }
    }
    m_next = m_steps.size();
}

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
    m_next = m_steps.size();

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
    m_next   = m_steps.size();
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

    const Step& step = m_steps[m_next++];
    ibl.createTargets();
    bakeState(gl);
    ibl.bindCaptureFbo();
    if (step.kind == Step::Kind::Capture) bindSky(atmosphere, m_target);
    runStep(gl, ibl, step);
    ibl.unbindCaptureFbo();

    if (baking()) return false;
    ibl.swap();
    return true;
}

void GLIBLBaker::runStep(Vkm::GL::Context& gl, GLIBL& ibl, const Step& step) {
    using Kind = Step::Kind;
    // Face @p face of the step's prefilter level, in the step's band.
    const auto prefilter = [&](int face) {
        m_convolver.prefilterBand(
            gl,
            face,
            step.mip,
            GLIBL::PREFILTER_MIPS,
            GLIBL::PREFILTER_SIZE >> step.mip,
            step.part,
            step.parts,
            [&](int attachFace, int mip) { ibl.attachPrefilterFace(gl, attachFace, mip); }
        );
    };

    switch (step.kind) {
        case Kind::Capture:
            for (int face = 0; face < 6; ++face) captureFace(gl, ibl, m_sky, face);
            break;
        case Kind::Mips:
            // The step's level is the mirror level, in one band: every face of it, whole.
            ibl.generateEnvMips();
            ibl.bindBackEnvCube(GLBindings::BakeTextureSlots::SOURCE);
            for (int face = 0; face < 6; ++face) prefilter(face);
            break;
        case Kind::Irradiance:
            ibl.bindBackEnvCube(GLBindings::BakeTextureSlots::SOURCE);
            m_convolver.irradianceSlice(gl, step.face, step.part, step.parts, [&](int face) {
                ibl.attachIrradianceFace(gl, face);
            });
            break;
        case Kind::Prefilter:
            ibl.bindBackEnvCube(GLBindings::BakeTextureSlots::SOURCE);
            prefilter(step.face);
            break;
    }
}

} // namespace Vkm::Engine
