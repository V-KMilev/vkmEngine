#include "gl_material.h"

#include <iterator>
#include <cstring>

#include "logger.h"

#include "gl_uniform_buffer.h"
#include "gl_config.h"
#include "gl_texture_mapping.h"

#include "material_asset.h"
#include "gl_view.h"
#include "gl_texture.h"

namespace Engine {

// Check if MaterialUBOData is 16-byte aligned for std140
static_assert(sizeof(MaterialUBOData) % 16 == 0, "MaterialUBOData must be 16-byte aligned for std140");
static_assert(sizeof(MaterialUBOData) == 144, "MaterialUBOData must be 144 bytes");
static_assert(offsetof(MaterialUBOData, albedo) == 0, "albedo offset");
static_assert(offsetof(MaterialUBOData, emission) == 16, "emission offset");
static_assert(offsetof(MaterialUBOData, metallic) == 32, "metallic offset");
static_assert(offsetof(MaterialUBOData, roughness) == 36, "roughness offset");
static_assert(offsetof(MaterialUBOData, anisotropyDirection) == 80, "anisotropyDirection offset");
static_assert(offsetof(MaterialUBOData, subsurfaceColor) == 112, "subsurfaceColor offset");
static_assert(offsetof(MaterialUBOData, heightScale) == 128, "heightScale offset");
static_assert(offsetof(MaterialUBOData, normalScale) == 132, "normalScale offset");
static_assert(offsetof(MaterialUBOData, textureFlags) == 136, "textureFlags offset");


GLMaterial::GLMaterial(const MaterialAsset& material) {
    update(material);
}

GLMaterial::~GLMaterial() {
    m_ubo.reset();

    LOG_TRACE("Destroying GLMaterial");
}

void GLMaterial::update(const MaterialAsset& material) {
    // Build UBO data from material asset
    MaterialUBOData uboData;
    std::memset(&uboData, 0, sizeof(MaterialUBOData));  // Zero out all fields including padding

    // Base PBR properties
    uboData.albedo = material.albedo;
    uboData.emission = material.emission;
    uboData.metallic = material.metallic;
    uboData.roughness = material.roughness;

    // Essential for raytracing and advanced PBR
    uboData.ior = material.ior;
    uboData.transmission = material.transmission;
    uboData.alpha = material.alpha;
    uboData.ao = material.ao;

    // Advanced PBR properties
    uboData.clearcoat = material.clearcoat;
    uboData.clearcoatRoughness = material.clearcoatRoughness;
    uboData.anisotropy = material.anisotropy;
    uboData.anisotropyDirection = material.anisotropyDirection;

    // Subsurface scattering
    uboData.subsurface = material.subsurface;
    uboData.subsurfaceColor = material.subsurfaceColor;

    // Height/Displacement and normal mapping
    uboData.heightScale = material.heightScale;
    uboData.normalScale = material.normalScale;

    // Process all texture mappings in a single loop
    // Build texture flags bitfield and bindings list
    m_textureBindings.clear();
    m_textureBindings.reserve(std::size(g_textureMappings));

    uint32_t textureFlags = 0;

    for (const auto& mapping : g_textureMappings) {
        const TextureHandle& handle = material.*mapping.handlePtr;
        const bool hasTexture = (handle.value != 0);

        // Set bit flag if texture is present
        if (hasTexture) {
            textureFlags |= static_cast<uint32_t>(mapping.flag);
            m_textureBindings.push_back({handle, mapping.slot});
        }
    }

    uboData.textureFlags = static_cast<int>(textureFlags);

    // Update or create UBO
    if (m_ubo) {
        m_ubo->update(&uboData, sizeof(MaterialUBOData));
    } else {
        m_ubo = std::make_unique<Core::UniformBuffer>(&uboData, sizeof(MaterialUBOData));
    }
}

void GLMaterial::bind(uint32_t bindingPoint) const {
    if (m_ubo) {
        m_ubo->bindBase(bindingPoint);
    }
}

void GLMaterial::bindTextures(const GLView& view) const {
    for (const auto& binding : m_textureBindings) {
        const GLTexture* texture = view.getTexture(binding.handle);
        if (texture) {
            texture->bind(binding.slot);
        }
    }
}

} // namespace Engine

