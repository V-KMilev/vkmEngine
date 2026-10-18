#include "asset/gl_layer_maps.h"

#include <algorithm>
#include <cmath>

#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>

#include "gl_compute_shader.h"
#include "gl_error_handle.h"
#include "gl_texture.h"

#include "convention/gl_bindings.h"

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

GLLayerMaps::~GLLayerMaps() {
    for (Array& array : m_arrays) release(array);
}

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
    VKM_GL_CHECK(glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT));
}

void GLLayerMaps::bind() const {
    for (size_t i = 0; i < m_arrays.size(); ++i) {
        VKM_GL_CHECK(glBindTextureUnit(ARRAY_UNITS[i], m_arrays[i].view));
    }
}

void GLLayerMaps::fillArray(Array& array, const Fill& fill, const Vkm::GL::ComputeShader& copy) {
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

    if (array.width != width || array.height != height || !array.texture) {
        release(array);
        array.width  = width;
        array.height = height;
        array.levels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
        const auto layers = static_cast<GLsizei>(MaterialAsset::MAX_LAYERS);
        VKM_GL_CHECK(glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &array.texture));
        VKM_GL_CHECK(glTextureStorage3D(array.texture, array.levels, GL_RGBA8, width, height, layers));
        array.view = array.texture;
        if (fill.srgb) {
            // Written as sRGB bytes, sampled decoded: an image cannot be written as sRGB.
            VKM_GL_CHECK(glGenTextures(1, &array.view));
            const GLuint view = array.view;
            VKM_GL_CHECK(
                glTextureView(
                    view,
                    GL_TEXTURE_2D_ARRAY,
                    array.texture,
                    GL_SRGB8_ALPHA8,
                    0,
                    array.levels,
                    0,
                    layers
                )
            );
        }
        VKM_GL_CHECK(glTextureParameteri(array.view, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR));
        VKM_GL_CHECK(glTextureParameteri(array.view, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        VKM_GL_CHECK(glTextureParameteri(array.view, GL_TEXTURE_WRAP_S, GL_REPEAT));
        VKM_GL_CHECK(glTextureParameteri(array.view, GL_TEXTURE_WRAP_T, GL_REPEAT));
        const float anisotropy = std::min(Vkm::GL::maxSupportedAnisotropy(), MAX_ANISOTROPY);
        if (anisotropy > 1.0f) {
            VKM_GL_CHECK(glTextureParameterf(array.view, GL_TEXTURE_MAX_ANISOTROPY, anisotropy));
        }
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
        for (uint32_t level = 0; level < array.levels; ++level) {
            const uint32_t w = std::max(array.width >> level, 1u);
            const uint32_t h = std::max(array.height >> level, 1u);
            // The source level whose texels are this level's: a smaller map is read at its base.
            copy.setUniform1f("u_lod", std::max(std::log2(sourceWidth / static_cast<float>(w)), 0.0f));
            const auto mip    = static_cast<GLint>(level);
            const bool layers = true;  // the whole array is bound; the shader writes one layer
            VKM_GL_CHECK(
                glBindImageTexture(TARGET_UNIT, array.texture, mip, layers, 0, GL_WRITE_ONLY, GL_RGBA8)
            );
            namespace Groups = GLBindings::ComputeGroups;
            copy.dispatch(Groups::covering(w, Groups::IMAGE), Groups::covering(h, Groups::IMAGE));
        }
    }
}

void GLLayerMaps::release(Array& array) {
    if (array.view && array.view != array.texture) VKM_GL_CHECK(glDeleteTextures(1, &array.view));
    if (array.texture) VKM_GL_CHECK(glDeleteTextures(1, &array.texture));
    array = Array{};
}

} // namespace Vkm::Engine
