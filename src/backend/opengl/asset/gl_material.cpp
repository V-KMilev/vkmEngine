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
    const bool seeThrough = material.albedo.a < 1.0f || static_cast<bool>(material.albedoTexture);
    m_cutsShadow  = m_type == MaterialType::AlphaMask || (m_type == MaterialType::Transparent && seeThrough);
    m_layered     = material.layered();
    m_plain       = !m_layered
        && material.anisotropy == 0.0f
        && material.subsurface == 0.0f
        && material.transmission == 0.0f
        && material.clearcoat == 0.0f
        && material.sheenColor == glm::vec3(0.0f)
        && (material.heightScale <= 0.0f || !material.heightTexture);

    m_textureBindings.clear();

    // A layered material's own maps are unread: its layers are read in their place, and their
    // shares where its roughness map would be (shaders/surface.glsl).
    int flags = 0;
    if (m_layered) {
        const TextureHandle weights = material.layerWeights;
        if (weights) m_textureBindings.push_back({weights, GLBindings::TextureSlots::ROUGHNESS});
    } else {
        for (const auto& map : MATERIAL_MAPS) {
            const TextureHandle& handle = material.*map.handle;
            if (handle) {
                m_textureBindings.push_back({handle, map.slot});
                flags |= 1 << map.slot;
            }
        }
        m_layerMaps.reset();
    }

    MaterialUBO& data = m_data;
    data.albedo              = material.albedo;
    data.emission            = glm::vec4(material.emission, material.emissiveStrength);
    data.anisotropyDirection = glm::vec4(material.anisotropyDirection, 0.0f);
    data.sheenColor          = glm::vec4(material.sheenColor, material.sheenRoughness);
    data.subsurfaceColor     = glm::vec4(material.subsurfaceColor, 0.0f);
    data.attenuationColor    = glm::vec4(material.attenuationColor, material.attenuationDistance);
    data.uvTransform         = glm::vec4(material.uvScale, material.uvOffset);

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
    data.waves              = material.waves ? 1 : 0;
    data.boxMapping         = material.boxMapping;

    if (m_ubo) m_ubo->update(&data, sizeof(data));
    else       m_ubo = std::make_unique<Vkm::GL::UniformBuffer>(&data, sizeof(data), GL_DYNAMIC_DRAW);
}

void GLMaterial::updateLayers(
    const MaterialAsset& layered,
    const std::array<const MaterialAsset*, MaterialAsset::MAX_LAYERS>& layers,
    const std::array<GLLayerMaps::Layer, MaterialAsset::MAX_LAYERS>& maps,
    uint64_t key,
    const Vkm::GL::ComputeShader& copy
) {
    if (m_layerMaps && key == m_layersKey) return;
    m_layersKey = key;
    for (size_t i = 0; i < layers.size(); ++i) {
        const MaterialAsset* layer = layers[i];
        // An empty layer is black: a share painted for it draws black.
        if (!layer) {
            m_data.layerTint[i]    = glm::vec4(0.0f);
            m_data.layerUV[i]      = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
            m_data.layerSurface[i] = glm::vec4(1.0f);
            continue;
        }
        m_data.layerTint[i]    = glm::vec4(glm::vec3(layer->albedo), layer->normalScale);
        const glm::vec2 tiling = layered.layerTiling[i];
        m_data.layerUV[i]      = glm::vec4(tiling == glm::vec2(0.0f) ? glm::vec2(1.0f) : tiling, 0.0f, 0.0f);
        m_data.layerSurface[i] = glm::vec4(layer->ao, layer->roughness, layer->metallic, 0.0f);
    }
    if (m_ubo) m_ubo->update(&m_data, sizeof(m_data));
    if (!m_layerMaps) m_layerMaps = std::make_unique<GLLayerMaps>();
    m_layerMaps->build(maps, key, copy);
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
    if (m_layerMaps) m_layerMaps->bind();
}

} // namespace Vkm::Engine
