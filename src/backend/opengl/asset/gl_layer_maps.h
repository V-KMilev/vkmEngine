#pragma once

#include <array>
#include <cstdint>

#include "resource/asset/material_asset.h"

namespace Vkm::GL {
    class ComputeShader;
    class Texture2D;
}

namespace Vkm::Engine {

/**
 * @brief A layered material's layers' maps (MaterialAsset::layers): an array texture apiece for the
 *        albedo, the normal and the AO/roughness/metallic maps, a layer of each per layer, which the
 *        LAYERED programs read.
 *
 * Built on the GPU from the layers' own uploaded maps (shaders/material/layer_copy), whatever their
 * format or size: each array takes its largest map's size, to MAX_SIZE, with every level read from
 * the matching level of its source. A layer without a map holds what its factors alone describe -
 * white albedo, a flat normal, full AO, roughness and metallic for the factors to scale.
 */
class GLLayerMaps {
    public:
        /// The largest edge an array takes.
        static constexpr uint32_t MAX_SIZE = 2048;

        /// What a source map holds, so what of it a layer keeps: LAYER_COPY_* in the shader prelude.
        enum class Copy : int {
            Fill,                 ///< No map: a constant.
            Albedo,               ///< An albedo map, kept sRGB-encoded.
            Normal,               ///< A tangent-space normal map's x and y.
            AoRoughnessMetallic,  ///< The engine's combined map.
            RoughnessMetallic,    ///< glTF's: roughness in green, metallic in blue, no occlusion.
            Count
        };

        /// One layer's maps: null where it has none, or where one has no pixels yet.
        struct Layer {
            const Vkm::GL::Texture2D* albedo  = nullptr;
            const Vkm::GL::Texture2D* normal  = nullptr;
            const Vkm::GL::Texture2D* surface = nullptr;
            bool gltfSurface = false;  ///< surface is glTF's roughness/metallic map, not the combined one
        };

    public:
        GLLayerMaps() = default;
        ~GLLayerMaps();

        GLLayerMaps(const GLLayerMaps& other) = delete;
        GLLayerMaps& operator=(const GLLayerMaps& other) = delete;

        GLLayerMaps(GLLayerMaps && other) = delete;
        GLLayerMaps& operator=(GLLayerMaps && other) = delete;

    public:
        /**
         * @brief Build the arrays from @p layers, unless they were last built from @p key.
         *
         * @param layers Each layer's maps.
         * @param key    Names what @p layers are built from: their materials' and maps' versions.
         * @param copy   The layer copy program (shaders/material/layer_copy).
         */
        void build(
            const std::array<Layer, MaterialAsset::MAX_LAYERS>& layers,
            uint64_t key,
            const Vkm::GL::ComputeShader& copy
        );

        /// Bind the arrays to the units the LAYERED programs read them on.
        void bind() const;

    private:
        /// One array: its storage, the view it is sampled through, and its size.
        struct Array {
            uint32_t texture = 0;
            uint32_t view    = 0;  ///< The albedo's sRGB view; the storage itself for the others.
            uint32_t width   = 0;
            uint32_t height  = 0;
            uint32_t levels  = 0;
        };

        /// The maps one array is filled from, what each holds, and what fills a layer without one.
        struct Fill {
            std::array<const Vkm::GL::Texture2D*, MaterialAsset::MAX_LAYERS> sources{};
            std::array<Copy, MaterialAsset::MAX_LAYERS>                      copies{};
            float empty[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            bool  srgb     = false;
        };

    private:
        /**
         * @brief Size @p array for @p fill's largest map, then fill every level of every layer.
         *
         * @param array Made or remade when its size changes.
         * @param fill  Its sources.
         * @param copy  The layer copy program.
         */
        static void fillArray(Array& array, const Fill& fill, const Vkm::GL::ComputeShader& copy);

        /// Delete @p array's textures.
        static void release(Array& array);

    private:
        std::array<Array, 3> m_arrays;      ///< Albedo, normal, AO/roughness/metallic.
        uint64_t             m_key   = 0;   ///< What they were last built from.
        bool                 m_built = false;
};

} // namespace Vkm::Engine
