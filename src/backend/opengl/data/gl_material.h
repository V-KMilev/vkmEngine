#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "resource/asset/material_asset.h"

namespace Vkm::GL {
    class UniformBuffer;
}

namespace Engine {
    class GLView;
}

namespace Engine {

/**
 * @brief A texture handle bound to a sampler slot.
 */
struct TextureBinding {
    TextureHandle handle;
    uint32_t      slot = 0;
};

/**
 * @brief std140 mirror of MaterialAsset - must match MaterialBlock in
 *        shaders/forward field-for-field, in order.
 *
 * vec4s first, then the scalar tail. std140 packs consecutive scalars at
 * 4-byte alignment, identical to the C++ layout, so no padding tricks are
 * needed as long as the GLSL block declares the same sequence. Colors pack
 * their companion scalar into .w to avoid the unreliable vec3-tail rule.
 */
struct MaterialUBO {
    glm::vec4 albedo;               ///< rgb + opacity
    glm::vec4 emission;             ///< rgb + emissiveStrength in w
    glm::vec4 anisotropyDirection;  ///< xyz (tangent space), w unused
    glm::vec4 sheenColor;           ///< rgb + sheenRoughness in w
    glm::vec4 subsurfaceColor;      ///< rgb, w unused
    glm::vec4 attenuationColor;     ///< rgb + attenuationDistance in w

    float metallic;
    float roughness;
    float ior;
    float ao;
    float normalScale;
    float clearcoat;
    float clearcoatRoughness;
    float anisotropy;
    float subsurface;
    float transmission;
    float thicknessFactor;
    float heightScale;
    float alphaCutoff;
    int   type;          ///< MaterialType as int (the shader branches on Unlit / AlphaMask)
    int   textureFlags;  ///< GLBindings::MaterialTextureFlags bits
    int   pad0;
};
static_assert(sizeof(MaterialUBO) == 160, "MaterialUBO must match the std140 MaterialBlock layout");

/**
 * @brief GPU copy of a material: the full PBR UBO plus its texture handles.
 *
 * Mirrors MaterialAsset one-to-one. The pass-relevant facts that drive CPU
 * state (draw bucket, cull mode) stay readable here so the forward pass can
 * bucket drawables without touching the ResourceManager.
 */
class GLMaterial {
    public:
        explicit GLMaterial(const MaterialAsset& material);
        ~GLMaterial();

        GLMaterial(const GLMaterial& other) = delete;
        GLMaterial& operator=(const GLMaterial& other) = delete;

        GLMaterial(GLMaterial && other) = delete;
        GLMaterial& operator=(GLMaterial && other) = delete;

    public:
        void update(const MaterialAsset& material);

        void bind(uint32_t bindingPoint) const;
        void bindTextures(const GLView& view) const;

        MaterialType getType() const { return m_type; }

        const std::vector<TextureBinding>& getTextureBindings() const { return m_textureBindings; }

    private:
        std::unique_ptr<Vkm::GL::UniformBuffer> m_ubo;
        std::vector<TextureBinding>           m_textureBindings;
        MaterialType                          m_type = MaterialType::Opaque;
};

} // namespace Engine
