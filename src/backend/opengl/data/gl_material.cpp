#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_material.h"

#include <GL/glew.h>

#include "convention/gl_bindings.h"
#include "gl_uniform_buffer.h"
#include "gl_view.h"
#include "texture/gl_texture.h"  // Core::Texture2D (bindSlot)

namespace Engine {

namespace {

// Every map the forward shader can sample. update() binds each map the asset
// actually has and sets its flag bit so the shader samples only those.
struct MapBinding {
    TextureHandle MaterialAsset::* handle;
    uint32_t                       slot;
    int                            flag;
};
constexpr MapBinding MATERIAL_MAPS[] = {
    {&MaterialAsset::albedoTexture,              GLBindings::TextureSlots::Albedo,              GLBindings::MaterialTextureFlags::Albedo},
    {&MaterialAsset::normalTexture,              GLBindings::TextureSlots::Normal,              GLBindings::MaterialTextureFlags::Normal},
    {&MaterialAsset::metallicRoughnessTexture,   GLBindings::TextureSlots::MetallicRoughness,   GLBindings::MaterialTextureFlags::MetallicRoughness},
    {&MaterialAsset::aoTexture,                  GLBindings::TextureSlots::AO,                  GLBindings::MaterialTextureFlags::AO},
    {&MaterialAsset::emissionTexture,            GLBindings::TextureSlots::Emission,            GLBindings::MaterialTextureFlags::Emission},
    {&MaterialAsset::heightTexture,              GLBindings::TextureSlots::Height,              GLBindings::MaterialTextureFlags::Height},
    {&MaterialAsset::clearcoatTexture,           GLBindings::TextureSlots::Clearcoat,           GLBindings::MaterialTextureFlags::Clearcoat},
    {&MaterialAsset::transmissionTexture,        GLBindings::TextureSlots::Transmission,        GLBindings::MaterialTextureFlags::Transmission},
    {&MaterialAsset::metallicTexture,            GLBindings::TextureSlots::Metallic,            GLBindings::MaterialTextureFlags::Metallic},
    {&MaterialAsset::roughnessTexture,           GLBindings::TextureSlots::Roughness,           GLBindings::MaterialTextureFlags::Roughness},
    {&MaterialAsset::aoMetallicRoughnessTexture, GLBindings::TextureSlots::AOMetallicRoughness, GLBindings::MaterialTextureFlags::AOMetallicRoughness},
};

} // namespace

GLMaterial::GLMaterial(const MaterialAsset& material) {
    update(material);
}

GLMaterial::~GLMaterial() = default;

void GLMaterial::update(const MaterialAsset& material) {
    m_type = material.type;

    m_textureBindings.clear();

    int flags = 0;
    for (const auto& map : MATERIAL_MAPS) {
        const TextureHandle& handle = material.*map.handle;
        if (handle) {
            m_textureBindings.push_back({handle, map.slot});
            flags |= map.flag;
        }
    }

    MaterialUBO data;
    data.albedo              = material.albedo;
    data.emission            = glm::vec4(material.emission, material.emissiveStrength);
    data.anisotropyDirection = glm::vec4(material.anisotropyDirection, 0.0f);
    data.sheenColor          = glm::vec4(material.sheenColor, material.sheenRoughness);
    data.subsurfaceColor     = glm::vec4(material.subsurfaceColor, 0.0f);
    data.attenuationColor    = glm::vec4(material.attenuationColor, material.attenuationDistance);

    data.metallic           = material.metallic;
    data.roughness          = material.roughness;
    data.ior                = material.ior;
    data.ao                 = material.ao;
    data.normalScale        = material.normalScale;
    data.clearcoat          = material.clearcoat;
    data.clearcoatRoughness = material.clearcoatRoughness;
    data.anisotropy         = material.anisotropy;
    data.subsurface         = material.subsurface;
    data.transmission       = material.transmission;
    data.thicknessFactor    = material.thicknessFactor;
    data.heightScale        = material.heightScale;
    data.alphaCutoff        = material.alphaCutoff;
    data.type               = static_cast<int>(material.type);
    data.textureFlags       = flags;
    data.pad0               = 0;

    if (m_ubo) m_ubo->update(&data, sizeof(data));
    else       m_ubo = std::make_unique<Core::UniformBuffer>(&data, sizeof(data), GL_DYNAMIC_DRAW);
}

void GLMaterial::bind(uint32_t bindingPoint) const {
    if (m_ubo) m_ubo->bindBase(bindingPoint);
}

void GLMaterial::bindTextures(const GLView& view) const {
    for (const auto& binding : m_textureBindings) {
        if (const Core::Texture2D* texture = view.getTexture(binding.handle)) {
            texture->bindSlot(binding.slot);
        }
    }
}

} // namespace Engine
