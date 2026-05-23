#include "gl_material.h"

#include <cstring>
#include <iterator>

#include "logger.h"

#include "config/gl_config.h"
#include "config/gl_texture_mapping.h"
#include "core/gl_view.h"
#include "gl_texture.h"
#include "gl_uniform_buffer.h"
#include "resource/material_asset.h"

namespace Engine {

// Check if MaterialUBOData is 16-byte aligned for std140
static_assert(sizeof(MaterialUBOData) % 16 == 0, "MaterialUBOData must be 16-byte aligned for std140");
static_assert(sizeof(MaterialUBOData) == 176, "MaterialUBOData must be 176 bytes");
static_assert(offsetof(MaterialUBOData, albedo) == 0, "albedo offset");
static_assert(offsetof(MaterialUBOData, emission) == 16, "emission offset");
static_assert(offsetof(MaterialUBOData, metallic) == 32, "metallic offset");
static_assert(offsetof(MaterialUBOData, roughness) == 36, "roughness offset");
static_assert(offsetof(MaterialUBOData, anisotropyDirection) == 80, "anisotropyDirection offset");
static_assert(offsetof(MaterialUBOData, subsurfaceColor) == 112, "subsurfaceColor offset");
static_assert(offsetof(MaterialUBOData, heightScale) == 128, "heightScale offset");
static_assert(offsetof(MaterialUBOData, normalScale) == 132, "normalScale offset");
static_assert(offsetof(MaterialUBOData, textureFlags) == 136, "textureFlags offset");
static_assert(offsetof(MaterialUBOData, attenuationColor) == 144, "attenuationColor offset");
static_assert(offsetof(MaterialUBOData, attenuationDistance) == 156, "attenuationDistance offset");
static_assert(offsetof(MaterialUBOData, thicknessFactor) == 160, "thicknessFactor offset");
static_assert(offsetof(MaterialUBOData, alphaCutoff) == 164, "alphaCutoff offset");


GLMaterial::GLMaterial(const MaterialAsset& material) {
    update(material);
}

GLMaterial::~GLMaterial() {
    m_ubo.reset();

    LOG_TRACE("Destructed GLMaterial");
}

void GLMaterial::update(const MaterialAsset& material) {
    // No memcmp-skip here (unlike GLLights / GLCamera): update() is only
    // called from GLView::syncTable when the asset's version has actually
    // changed, so the data is always dirty by construction.
    MaterialUBOData uboData;
    std::memset(&uboData, 0, sizeof(MaterialUBOData));  // zero padding too

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

    // Sheen / cloth (offsets unchanged - repurposed former pad floats)
    uboData.sheenColor[0] = material.sheenColor.x;
    uboData.sheenColor[1] = material.sheenColor.y;
    uboData.sheenColor[2] = material.sheenColor.z;
    uboData.sheenRoughness = material.sheenRoughness;

    // Height/Displacement and normal mapping
    uboData.heightScale = material.heightScale;
    uboData.normalScale = material.normalScale;

    // KHR_materials_volume. Default (thicknessFactor == 0) skips Beer-Lambert
    // in the shader; the other two are still sent so live edits work as soon
    // as thickness becomes non-zero.
    uboData.attenuationColor    = material.attenuationColor;
    uboData.attenuationDistance = material.attenuationDistance;
    uboData.thicknessFactor     = material.thicknessFactor;

    // Alpha-tested foliage / leaves (glTF alphaMode = MASK). 0 disables the
    // discard in the shader; the loader sets the glTF default (0.5) only
    // when the asset is actually classified as AlphaMask.
    uboData.alphaCutoff         = material.alphaCutoff;

    // Cache the optional-feature bitset so the forward pass can pick the
    // right shader variant for this material without re-reading the asset.
    m_featureFlags = material.featureFlags();

    // Process all texture mappings in a single loop
    // Build texture flags bitfield and bindings list
    m_textureBindings.clear();
    m_textureBindings.reserve(std::size(g_textureMappings));

    uint32_t textureFlags = 0;

    for (const auto& mapping : g_textureMappings) {
        const TextureHandle& handle = material.*mapping.handlePtr;
        const bool hasTexture = bool(handle);

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

