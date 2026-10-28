#include "asset/gl_layer_maps.h"

#include <algorithm>
#include <cmath>

#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>

#include "gl_compute_shader.h"
#include "gl_context.h"
#include "gl_texture.h"
#include "gl_texture_2d_array.h"

#include "convention/gl_bindings.h"
#include "resource/texture_format.h"

namespace Vkm::Engine {

namespace {

/// The anisotropy the arrays take, as the ocean's fields do: a terrain is mostly seen at a slant.
constexpr float MAX_ANISOTROPY = 8.0f;

/// The units the LAYERED programs read the arrays on (shaders/surface.glsl).
constexpr uint32_t ARRAY_UNITS[3] = {
    GLBindings::TextureSlots::CLEARCOAT,
    GLBindings::TextureSlots::TRANSMISSION,
    GLBindings::TextureSlots::METALLIC,
};

} // namespace

GLLayerMaps::GLLayerMaps()  = default;
GLLayerMaps::~GLLayerMaps() = default;

void GLLayerMaps::build(
    const std::array<Layer, MaterialAsset::MAX_LAYERS>& layers,
    uint64_t key,
    const Vkm::GL::ComputeShader& copy
) {
    if (m_built && key == m_key) return;
    m_key   = key;
    m_built = true;

    Fill albedo;
    Fill normal;
    Fill surface;
    albedo.srgb = true;
    normal.empty[0] = normal.empty[1] = 0.5f;
    for (size_t i = 0; i < layers.size(); ++i) {
        const Layer& layer = layers[i];
        albedo.sources[i]  = layer.albedo;
        albedo.copies[i]   = layer.albedo ? Copy::Albedo : Copy::Fill;
        normal.sources[i]  = layer.normal;
        normal.copies[i]   = layer.normal ? Copy::Normal : Copy::Fill;
        surface.sources[i] = layer.surface;
        surface.copies[i]  = !layer.surface ? Copy::Fill
            : layer.gltfSurface             ? Copy::RoughnessMetallic
            : Copy::AoRoughnessMetallic;
    }
    fillArray(m_arrays[0], albedo, copy);
    fillArray(m_arrays[1], normal, copy);
    fillArray(m_arrays[2], surface, copy);
    Vkm::GL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
}

void GLLayerMaps::bind() const {
    for (size_t i = 0; i < m_arrays.size(); ++i) {
        if (m_arrays[i]) m_arrays[i]->bindSlot(ARRAY_UNITS[i]);
    }
}

void GLLayerMaps::fillArray(
    std::unique_ptr<Vkm::GL::Texture2DArray>& array,
    const Fill& fill,
    const Vkm::GL::ComputeShader& copy
) {
    // The largest map's size, so no layer is read coarser than it was made.
    uint32_t width  = 4;
    uint32_t height = 4;
    for (const Vkm::GL::Texture2D* source : fill.sources) {
        if (!source) continue;
        width  = std::max(width, source->getParams().width);
        height = std::max(height, source->getParams().height);
    }
    width  = std::min(width, MAX_SIZE);
    height = std::min(height, MAX_SIZE);

    if (!array || array->getWidth() != width || array->getHeight() != height) {
        Vkm::GL::Texture2DArrayParams params;
        params.width          = width;
        params.height         = height;
        params.layers         = MaterialAsset::MAX_LAYERS;
        params.levels         = mipChainLength(width, height);
        params.internalFormat = GL_RGBA8;
        params.wrapS          = Vkm::GL::TextureWrap::Repeat;
        params.wrapT          = Vkm::GL::TextureWrap::Repeat;
        params.minFilter      = Vkm::GL::TextureMinFilter::LinearMipmapLinear;
        params.maxAnisotropy  = MAX_ANISOTROPY;
        // Written as sRGB bytes, sampled decoded: an image cannot be written as sRGB.
        if (fill.srgb) params.viewFormat = GL_SRGB8_ALPHA8;
        array.reset();
        array = std::make_unique<Vkm::GL::Texture2DArray>("layer_maps", params);
    }

    copy.bind();
    copy.setUniform4fv("u_fill", glm::make_vec4(fill.empty));
    constexpr uint32_t TARGET_UNIT = 0;
    for (uint32_t layer = 0; layer < MaterialAsset::MAX_LAYERS; ++layer) {
        const Vkm::GL::Texture2D* source = fill.sources[layer];
        const Copy                what   = fill.copies[layer];
        if (source) source->bindSlot(0);
        const float sourceWidth = source ? static_cast<float>(source->getParams().width) : 1.0f;
        copy.setUniform1i("u_copy", static_cast<int>(what));
        copy.setUniform1i("u_layer", static_cast<int>(layer));
        for (uint32_t level = 0; level < array->levels(); ++level) {
            const uint32_t w = mipExtent(width, level);
            const uint32_t h = mipExtent(height, level);
            // The source level whose texels are this level's: a smaller map is read at its base.
            copy.setUniform1f("u_lod", std::max(std::log2(sourceWidth / static_cast<float>(w)), 0.0f));
            // The whole array is bound; the shader writes one layer.
            array->bindImage(TARGET_UNIT, GL_WRITE_ONLY, level);
            namespace Groups = GLBindings::ComputeGroups;
            copy.dispatch(Groups::covering(w, Groups::IMAGE), Groups::covering(h, Groups::IMAGE));
        }
    }
}

} // namespace Vkm::Engine
