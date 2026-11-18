#include "asset/gl_layer_maps.h"

#include <algorithm>
#include <cmath>

#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>

#include "vkm_opengl/core/context.h"
#include "vkm_opengl/shader/compute_shader.h"
#include "vkm_opengl/texture/texture.h"
#include "vkm_opengl/texture/texture_2d_array.h"

#include "convention/gl_bindings.h"
#include "resource/asset/texture_params.h"

namespace Vkm::Engine {

namespace {

constexpr float MAX_ANISOTROPY = 8.0f;       ///< The anisotropy the color array takes, as the ripple fields
                                             ///< do: a terrain is mostly seen at a slant.
constexpr float MAX_DATA_ANISOTROPY = 4.0f;  ///< The other arrays': a slant's specular shows less of it than
                                             ///< its color does.

constexpr uint32_t ARRAY_UNITS[3] = {
    GLBindings::LayerTextureSlots::ALBEDO,
    GLBindings::LayerTextureSlots::NORMAL,
    GLBindings::LayerTextureSlots::SURFACE,
};  ///< The units the LAYERED programs read the arrays on, in the arrays' order.

} // namespace

GLLayerMaps::GLLayerMaps()  = default;
GLLayerMaps::~GLLayerMaps() = default;

void GLLayerMaps::build(
    const std::array<Layer, MaterialAsset::MAX_LAYERS>& layers,
    const Vkm::OpenGL::ComputeShader& copy
) {
    Fill albedo;
    Fill normal;
    Fill surface;
    albedo.srgb     = true;
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
    Vkm::OpenGL::memoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
}

void GLLayerMaps::bind() const {
    for (size_t i = 0; i < m_arrays.size(); ++i) {
        if (m_arrays[i]) m_arrays[i]->bindSlot(ARRAY_UNITS[i]);
    }
}

void GLLayerMaps::fillArray(
    std::unique_ptr<Vkm::OpenGL::Texture2DArray>& array,
    const Fill& fill,
    const Vkm::OpenGL::ComputeShader& copy
) {
    // The largest map's size, so no layer is read coarser than it was made.
    uint32_t width  = 4;
    uint32_t height = 4;
    for (const Vkm::OpenGL::Texture2D* source : fill.sources) {
        if (!source) continue;
        width  = std::max(width, source->getParams().width);
        height = std::max(height, source->getParams().height);
    }
    width  = std::min(width, MAX_SIZE);
    height = std::min(height, MAX_SIZE);

    if (!array || array->getWidth() != width || array->getHeight() != height) {
        Vkm::OpenGL::Texture2DArrayParams params;
        params.width          = width;
        params.height         = height;
        params.layers         = MaterialAsset::MAX_LAYERS;
        params.levels         = mipChainLength(width, height);
        params.internalFormat = GL_RGBA8;
        params.wrapS          = Vkm::OpenGL::TextureWrap::Repeat;
        params.wrapT          = Vkm::OpenGL::TextureWrap::Repeat;
        params.minFilter      = Vkm::OpenGL::TextureMinFilter::LinearMipmapLinear;
        params.maxAnisotropy  = fill.srgb ? MAX_ANISOTROPY : MAX_DATA_ANISOTROPY;
        // Written as sRGB bytes, sampled decoded: an image cannot be written as sRGB.
        if (fill.srgb) params.viewFormat = GL_SRGB8_ALPHA8;
        array.reset();
        array = std::make_unique<Vkm::OpenGL::Texture2DArray>("layer_maps", params);
    }

    copy.bind();
    copy.setUniform4fv("u_fill", glm::make_vec4(fill.empty));
    constexpr uint32_t TARGET_UNIT = 0;
    for (uint32_t layer = 0; layer < MaterialAsset::MAX_LAYERS; ++layer) {
        const Vkm::OpenGL::Texture2D* source = fill.sources[layer];
        const Copy what = fill.copies[layer];
        if (source) source->bindSlot(GLBindings::LayerTextureSlots::COPY_SOURCE);
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
