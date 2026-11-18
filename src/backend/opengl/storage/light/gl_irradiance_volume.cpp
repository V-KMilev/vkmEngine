#include "storage/light/gl_irradiance_volume.h"

#include <utility>

#include "vkm_opengl/shader/shader_base.h"
#include "vkm_opengl/texture/texture_3d.h"

#include "convention/gl_bindings.h"
#include "system/render/data/irradiance_volume_data.h"

namespace Vkm::Engine {

GLIrradianceVolume::GLIrradianceVolume()  = default;
GLIrradianceVolume::~GLIrradianceVolume() = default;

void GLIrradianceVolume::resize(uint32_t x, uint32_t y, uint32_t z) {
    if (x == 0 || y == 0 || z == 0) return;
    if (m_sh[0] && x == m_sh[0]->getWidth() && y == m_sh[0]->getHeight() && z == m_sh[0]->getDepth()) return;

    for (int i = 0; i < SH_COEFFS; ++i) {
        Vkm::OpenGL::Texture3DParams p;
        p.width          = x;
        p.height         = y;
        p.depth          = z;
        p.internalFormat = GL_RGBA16F;
        p.format         = GL_RGBA;
        p.type           = GL_FLOAT;
        // Linear so the lookup blends between neighboring probes; clamp so a
        // sample at the box edge holds the border probe instead of wrapping.
        p.minFilter = Vkm::OpenGL::TextureMinFilter::Linear;
        p.magFilter = Vkm::OpenGL::TextureMagFilter::Linear;
        p.wrap      = Vkm::OpenGL::TextureWrap::ClampToEdge;
        m_sh[i]     = std::make_unique<Vkm::OpenGL::Texture3D>("irradiance_sh", p);
    }

    // Contents are undefined until a bake fills every cell.
    m_ready = false;
}

void GLIrradianceVolume::release() {
    for (auto& sh : m_sh) sh.reset();
    m_ready = false;
}

void GLIrradianceVolume::setBaked(const IrradianceVolumeData& box) {
    m_ready       = true;
    m_center      = box.center;
    m_halfExtents = box.halfExtents;
    ++m_bakeId;
}

IrradianceVolumeData GLIrradianceVolume::placed(const IrradianceVolumeData& asked) const {
    IrradianceVolumeData box = asked;
    box.center      = m_center;
    box.halfExtents = m_halfExtents;
    return box;
}

void GLIrradianceVolume::swapGrid(GLIrradianceVolume& other) {
    for (int i = 0; i < SH_COEFFS; ++i) std::swap(m_sh[i], other.m_sh[i]);
    ++m_lightId;
}

void GLIrradianceVolume::bindImage(int i, uint32_t unit, GLenum access) const {
    if (i >= 0 && i < SH_COEFFS && m_sh[i]) m_sh[i]->bindImage(unit, access);
}

void GLIrradianceVolume::bindSlot(int i, uint32_t slot) const {
    if (i >= 0 && i < SH_COEFFS && m_sh[i]) m_sh[i]->bindSlot(slot);
}

void GLIrradianceVolume::bindForShading(
    const Vkm::OpenGL::ShaderBase& shader,
    const IrradianceVolumeData& box
) const {
    bindSlot(0, GLBindings::IrradianceVolumeSlots::SH0);
    bindSlot(1, GLBindings::IrradianceVolumeSlots::SH1);
    bindSlot(2, GLBindings::IrradianceVolumeSlots::SH2);
    bindSlot(3, GLBindings::IrradianceVolumeSlots::SH3);
    shader.setUniform1i("u_hasIrradianceVolume", 1);
    shader.setUniform3fv("u_ivMin", box.center - box.halfExtents);
    shader.setUniform3fv("u_ivSize", box.halfExtents * 2.0f);
    shader.setUniform1f("u_ivIntensity", box.intensity);
    shader.setUniform1f("u_ivBlend", box.blendDistance);
}

} // namespace Vkm::Engine
