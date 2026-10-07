#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "storage/gl_ibl.h"

#include "logger.h"

#include "storage/gl_lookup_table.h"

namespace Vkm::Engine {

void GLIBL::createTargets() {
    if (m_cubes[0].env.getID() != 0) return;
    if (!m_captureFbo) m_captureFbo = std::make_unique<Vkm::GL::FrameBuffer>();

    // RGBA16F: rendered into, and GL makes RGB16F colour-renderable only optionally. Not
    // R11F_G11F_B10F: this cube is an HDR sky's skybox, and a sky gradient bands at 6-bit mantissas.
    for (Cubes& set : m_cubes) {
        set.env.create(ENV_SIZE, ENV_MIPS, GL_RGBA16F, GL_RGBA, GL_FLOAT, true);
        set.irradiance.create(IRRADIANCE_SIZE, 1, GL_RGBA16F, GL_RGBA, GL_FLOAT, false);
        set.prefilter.create(PREFILTER_SIZE, PREFILTER_MIPS, GL_RGBA16F, GL_RGBA, GL_FLOAT, true);
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
    if (!m_captureFbo) m_captureFbo = std::make_unique<Vkm::GL::FrameBuffer>();
    m_brdf = std::make_unique<Vkm::GL::Texture2D>(
        "ibl_brdf_lut",
        lookupTableParams(BRDF_SIZE, BRDF_SIZE, GL_RG16F, GL_RG)
    );
}

void GLIBL::attachFace(
    const Vkm::GL::Context& gl,
    const Vkm::GL::TextureCube& cube,
    int face,
    int mip,
    int size
) const {
    const GLenum target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(face);
    m_captureFbo->attachTexture2D(GL_COLOR_ATTACHMENT0, target, cube.getID(), mip);
    gl.setViewport(0, 0, size, size);
}

} // namespace Vkm::Engine
