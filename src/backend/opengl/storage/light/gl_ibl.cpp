#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "storage/light/gl_ibl.h"

#include "vkm_log/logger.h"

namespace Vkm::Engine {

void GLIBL::createTargets() {
    if (m_cubes[0].env.getID() != 0) return;
    if (!m_captureFbo) m_captureFbo = std::make_unique<Vkm::OpenGL::FrameBuffer>();

    // RGBA16F: rendered into, and GL makes RGB16F color-renderable only optionally. Not
    // R11F_G11F_B10F: this cube is an HDR sky's skybox, and a sky gradient bands at 6-bit mantissas.
    for (Cubes& set : m_cubes) {
        set.env.create(ENV_SIZE, ENV_MIPS, GL_RGBA16F, true);
        set.irradiance.create(IRRADIANCE_SIZE, 1, GL_RGBA16F, false);
        set.prefilter.create(PREFILTER_SIZE, PREFILTER_MIPS, GL_RGBA16F, true);
    }

    LOG_INFO(
        "Targets allocated, two sets (env %d, irr %d, prefilter %d/%d mips)",
        ENV_SIZE,
        IRRADIANCE_SIZE,
        PREFILTER_SIZE,
        PREFILTER_MIPS
    );
}

void GLIBL::createBrdf() {
    if (m_brdf) return;
    if (!m_captureFbo) m_captureFbo = std::make_unique<Vkm::OpenGL::FrameBuffer>();
    m_brdf = std::make_unique<Vkm::OpenGL::Texture2D>(
        "ibl_brdf_lut",
        Vkm::OpenGL::renderTargetParams(
            BRDF_SIZE,
            BRDF_SIZE,
            GL_RGBA16F,
            GL_RGBA,
            Vkm::OpenGL::TextureMinFilter::Linear,
            Vkm::OpenGL::TextureMagFilter::Linear
        )
    );
}

void GLIBL::attachFace(
    const Vkm::OpenGL::Context& gl,
    const Vkm::OpenGL::TextureCube& cube,
    int face,
    int mip,
    int size
) const {
    m_captureFbo->attachLayer(GL_COLOR_ATTACHMENT0, cube.getID(), mip, face);
    gl.setViewport(0, 0, size, size);
}

} // namespace Vkm::Engine
