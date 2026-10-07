#include "storage/gl_irradiance_volume.h"

#include "gl_shader_base.h"
#include "gl_texture_3d.h"

#include "convention/gl_bindings.h"
#include "system/render/data/irradiance_volume_data.h"

namespace Vkm::Engine {

GLIrradianceVolume::GLIrradianceVolume()  = default;
GLIrradianceVolume::~GLIrradianceVolume() = default;

void GLIrradianceVolume::resize(uint32_t x, uint32_t y, uint32_t z) {
    if (x == 0 || y == 0 || z == 0) return;
    if (m_sh[0] && x == m_sh[0]->getWidth() && y == m_sh[0]->getHeight() && z == m_sh[0]->getDepth()) return;

    for (int i = 0; i < SH_COEFFS; ++i) {
        Vkm::GL::Texture3DParams p;
        p.width  = x;
        p.height = y;
        p.depth  = z;
        p.internalFormat = GL_RGBA16F;
        p.format         = GL_RGBA;
        p.type           = GL_FLOAT;
        // Linear so the lookup blends between neighbouring probes; clamp so a
        // sample at the box edge holds the border probe instead of wrapping.
        p.minFilter = Vkm::GL::TextureMinFilter::Linear;
        p.magFilter = Vkm::GL::TextureMagFilter::Linear;
        p.wrap      = Vkm::GL::TextureWrap::ClampToEdge;
        m_sh[i] = std::make_unique<Vkm::GL::Texture3D>("irradiance_sh", p);
    }

    // Contents are undefined until a bake fills every cell.
    m_ready = false;
}

void GLIrradianceVolume::setBaked(bool baked) {
    m_ready = baked;
    if (baked) ++m_bakeId;
}

uint32_t GLIrradianceVolume::cellCount() const {
    return m_sh[0] ? m_sh[0]->getWidth() * m_sh[0]->getHeight() * m_sh[0]->getDepth() : 0;
}

void GLIrradianceVolume::download(ProbeGridSH& sh) const {
    const size_t cells = cellCount();
    for (int i = 0; i < SH_COEFFS; ++i) {
        if (cells == 0 || !m_sh[i]) {
            sh[i].clear();
            continue;
        }
        sh[i].resize(cells);
        m_sh[i]->download(sh[i].data());
    }
}

void GLIrradianceVolume::upload(const ProbeGridSH& sh) const {
    const size_t cells = cellCount();
    if (cells == 0) return;
    for (int i = 0; i < SH_COEFFS; ++i) {
        if (!m_sh[i] || sh[i].size() != cells) return;
    }
    for (int i = 0; i < SH_COEFFS; ++i) m_sh[i]->upload(sh[i].data());
}

void GLIrradianceVolume::bindImage(int i, uint32_t unit, GLenum access) const {
    if (i >= 0 && i < SH_COEFFS && m_sh[i]) m_sh[i]->bindImage(unit, access);
}

void GLIrradianceVolume::bindSlot(int i, uint32_t slot) const {
    if (i >= 0 && i < SH_COEFFS && m_sh[i]) m_sh[i]->bindSlot(slot);
}

void GLIrradianceVolume::bindForShading(
    const Vkm::GL::ShaderBase& shader,
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
