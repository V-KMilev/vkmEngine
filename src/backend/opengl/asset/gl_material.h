#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "asset/gl_layer_maps.h"
#include "resource/asset/material_asset.h"

namespace Vkm::GL {
    class ComputeShader;
    class UniformBuffer;
}

namespace Vkm::Engine {

class GLView;

/**
 * @brief A texture handle bound to a sampler slot.
 */
struct TextureBinding {
    TextureHandle handle;
    uint32_t      slot = 0;
};

/**
 * @brief std140 mirror of MaterialAsset - must match MaterialBlock in
 *        shaders/material.glsl field-for-field, in order.
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
    glm::vec4 uvTransform;          ///< uvScale in xy, uvOffset in zw

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
    int   textureFlags;  ///< Bit `1 << slot` set for each bound map (GLBindings::TextureSlots)
    int   waves;         ///< 1 for WaterKind::Sea: the scene's ocean moves the surface (shaders/ocean.glsl)
    float boxMapping;    ///< MaterialAsset::boxMapping: metres a repeat, 0 for the mesh's UVs
    int   water;         ///< 1 for WaterKind::Body: water that is not the sea
    float flowSpeed;     ///< MaterialAsset::flowSpeed, m/s along the mesh's +U
    float rippleScale;   ///< MaterialAsset::rippleScale

    // A layered material's layers (MaterialAsset::layers); zero for any other.
    glm::vec4 layerTint[MaterialAsset::MAX_LAYERS];     ///< albedo factor; w the normal scale
    glm::vec4 layerUV[MaterialAsset::MAX_LAYERS];       ///< uvScale in xy, uvOffset in zw
    glm::vec4 layerSurface[MaterialAsset::MAX_LAYERS];  ///< ao, roughness and metallic factors
};
static_assert(sizeof(MaterialUBO) == 384, "MaterialUBO must match the std140 MaterialBlock layout");

/**
 * @brief GPU copy of a material: the full PBR UBO plus its texture handles.
 *
 * Mirrors MaterialAsset one-to-one. The one fact that drives CPU state, the
 * draw bucket, stays readable here (getType) so the backend can partition
 * drawables without touching the ResourceManager.
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

        /**
         * @brief Bind every map this material references to its texture slot.
         *
         * Every referenced map binds something: a map that has not resolved yet
         * gets the placeholder rather than being skipped. Skipping would leave
         * the previous draw's texture live in that slot, so a material still
         * streaming - or one whose file failed to decode - would render with
         * another object's maps, which reads as a shading bug with nothing
         * pointing at the cause.
         *
         * @param view Resolves the bindings' handles, and owns the placeholder.
         */
        void bindTextures(const GLView& view) const;

        /**
         * @brief The albedo map this material binds, or an empty handle.
         *
         * An alpha-masked material's cutout is that map's alpha.
         */
        TextureHandle albedoMap() const;

        MaterialType getType() const { return m_type; }

        /**
         * @brief Whether its shadow is cut by its alpha.
         *
         * A cutout's at its cutoff, a transparent one's dithered by its opacity
         * (shaders/shadow/depth_masked), so a see-through part casts none. A transparent one wholly
         * opaque - alpha 1, no map to say otherwise - casts as a solid.
         */
        bool cutsItsShadow() const { return m_cutsShadow; }

        /// Whether both faces draw: the passes leave culling off for its runs.
        bool doubleSided() const { return m_doubleSided; }

        /**
         * @brief Whether it uses none of the forward shader's optional features, so draws with the
         *        program compiled without them (shaders/forward/pbr_plain).
         *
         * Anisotropy, subsurface, transmission, clearcoat, sheen and parallax: the shader's
         * FEATURES gates (shaders/forward/pbr/fragment.shader), each tested here. Each must be off
         * exactly, not below the shader's own threshold: a value the shader would skip anyway
         * draws the same either way, so the two need not agree on where "off" begins.
         */
        bool plain() const { return m_plain; }

        /**
         * @brief Whether it blends layers (MaterialAsset::layers), so draws with the LAYERED
         *        program (shaders/forward/pbr_layered), whatever plain() says.
         */
        bool layered() const { return m_layered; }

        /**
         * @brief Whether it is water that is not the sea (WaterKind::Body): drawn, it wants the
         *        scene's ripple fields whether or not the scene has an ocean.
         */
        bool water() const { return m_water; }

        /**
         * @brief Take a layered material's layers: their factors, and their maps as arrays.
         *
         * @param layered The layered material, whose tiling each layer takes.
         * @param layers  Each layer's material, null for none.
         * @param maps    Each layer's maps, as GLView holds them.
         * @param key     Names what they are built from; equal keys skip the rebuild.
         * @param copy    The layer copy program the arrays are filled with.
         */
        void updateLayers(
            const MaterialAsset& layered,
            const std::array<const MaterialAsset*, MaterialAsset::MAX_LAYERS>& layers,
            const std::array<GLLayerMaps::Layer, MaterialAsset::MAX_LAYERS>& maps,
            uint64_t key,
            const Vkm::GL::ComputeShader& copy
        );

        const std::vector<TextureBinding>& getTextureBindings() const { return m_textureBindings; }

        /**
         * @brief Which upload this material's values came from, distinct across every GLMaterial.
         *
         * The handle survives an edit; this changes whenever update() runs.
         */
        uint64_t uploadId() const { return m_uploadId; }

    private:
        std::unique_ptr<Vkm::GL::UniformBuffer> m_ubo;
        MaterialUBO                             m_data{};  ///< What m_ubo holds.
        std::unique_ptr<GLLayerMaps>            m_layerMaps;  ///< A layered one's layers' maps.
        uint64_t                                m_layersKey = 0;  ///< What m_layerMaps was built from.
        std::vector<TextureBinding>             m_textureBindings;
        MaterialType                            m_type = MaterialType::Opaque;
        bool                                    m_doubleSided = false;
        bool                                    m_cutsShadow = false;  ///< See cutsItsShadow().
        bool                                    m_plain = true;        ///< See plain().
        bool                                    m_layered = false;     ///< See layered().
        bool                                    m_water   = false;     ///< See water().
        uint64_t                                m_uploadId = 0;
};

} // namespace Vkm::Engine
