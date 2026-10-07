#include "asset/gl_material.h"

#include <GL/glew.h>

#include "convention/gl_bindings.h"
#include "gl_uniform_buffer.h"
#include "gl_view.h"
#include "gl_texture.h"

namespace Vkm::Engine {

namespace {

// Every map the forward shader can sample. update() records a binding for
// each map the asset has and sets the bit at its slot, so the shader samples only those.
struct MapBinding {
    TextureHandle MaterialAsset::* handle;
    uint32_t                       slot;
};
constexpr MapBinding MATERIAL_MAPS[] = {
#define VKM_MATERIAL_MAP_BINDING(key, member, slot, doc) \
    {&MaterialAsset::member, GLBindings::TextureSlots::slot},
    VKM_MATERIAL_MAPS(VKM_MATERIAL_MAP_BINDING)
#undef VKM_MATERIAL_MAP_BINDING
};

// The last upload id handed out. Materials upload on the render thread alone.
uint64_t g_lastUpload = 0;

} // namespace

GLMaterial::GLMaterial(const MaterialAsset& material) {
    update(material);
}

GLMaterial::~GLMaterial() = default;

void GLMaterial::update(const MaterialAsset& material) {
    m_uploadId    = ++g_lastUpload;
    m_type        = material.type;
    m_doubleSided = material.doubleSided;

    m_textureBindings.clear();

    int flags = 0;
    for (const auto& map : MATERIAL_MAPS) {
        const TextureHandle& handle = material.*map.handle;
        if (handle) {
            m_textureBindings.push_back({handle, map.slot});
            flags |= 1 << map.slot;
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
    else       m_ubo = std::make_unique<Vkm::GL::UniformBuffer>(&data, sizeof(data), GL_DYNAMIC_DRAW);
}

TextureHandle GLMaterial::albedoMap() const {
    for (const TextureBinding& binding : m_textureBindings) {
        if (binding.slot == GLBindings::TextureSlots::ALBEDO) return binding.handle;
    }
    return {};
}

void GLMaterial::bind(uint32_t bindingPoint) const {
    if (m_ubo) m_ubo->bindBase(bindingPoint);
}

void GLMaterial::bindTextures(const GLView& view) const {
    for (const auto& binding : m_textureBindings) {
        const Vkm::GL::Texture2D* texture = view.getTexture(binding.handle);
        if (!texture) texture = &view.missingTexture();
        texture->bindSlot(binding.slot);
    }
}

} // namespace Vkm::Engine
