#include "resource/generate/texture_generators.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include <glm/common.hpp>
#include <nlohmann/json.hpp>

#include "io/json_vec.h"
#include "resource/resource_manager.h"
#include "resource/asset_source_kind.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Quantise a 0-1 color channel to the 8-bit value a texel holds.
 *
 * Shared by the texel and the dedup name, which must not quantise differently.
 * Rounds to nearest.
 *
 * @param channel Color channel; clamped to 0-1 before quantisation.
 * @return The channel as an 8-bit value.
 */
uint8_t quantizeChannel(float channel) {
    return static_cast<uint8_t>(std::lround(glm::clamp(channel, 0.0f, 1.0f) * 255.0f));
}

/**
 * @brief Build a 1x1 texture filled with a single clamped color, stored as
 *        @p usage stores a texel.
 *
 * @param color RGBA color, clamped to 0-1 per channel.
 * @param usage Color (sRGB RGBA), data (linear RGBA) or a normal (red and
 *              green as x and y).
 * @return The 1x1 texture.
 */
TextureAsset makeSolidColorAsset(glm::vec4 color, TextureUsage usage) {
    const int channels = usage == TextureUsage::Normal ? 2 : 4;
    TextureAsset texture;
    texture.params.width = 1;
    texture.params.height = 1;
    texture.params.internalFormat = inferInternalFormat(channels, usage);
    texture.params.format = inferFormat(channels);
    texture.params.type = TexturePixelType::UnsignedByte;
    texture.params.generateMipmaps = false;

    const uint8_t texel[4] = {
        quantizeChannel(color.r),
        quantizeChannel(color.g),
        quantizeChannel(color.b),
        quantizeChannel(color.a)
    };
    texture.pixelData.assign(texel, texel + channels);
    return texture;
}

} // namespace

TextureHandle generateWhiteTexture(ResourceManager& rm) {
    return createSolidColorTexture(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), rm);
}

TextureHandle generateBlackTexture(ResourceManager& rm) {
    return createSolidColorTexture(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), rm);
}

TextureHandle generateNormalTexture(ResourceManager& rm) {
    return createSolidColorTexture(glm::vec4(0.5f, 0.5f, 1.0f, 1.0f), rm, TextureUsage::Normal);
}

TextureHandle generateGrayTexture(ResourceManager& rm) {
    return createSolidColorTexture(glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), rm);
}

TextureHandle createSolidColorTexture(glm::vec4 color, ResourceManager& rm, TextureUsage usage) {
    // Keyed on the quantised color and usage, by the quantiser that writes the
    // texel, so equal solids dedup and the name matches the pixel.
    char key[64];
    std::snprintf(
        key,
        sizeof(key),
        "texture:solid:%02X%02X%02X%02X:%s",
        quantizeChannel(color.r),
        quantizeChannel(color.g),
        quantizeChannel(color.b),
        quantizeChannel(color.a),
        Reflect::enumName(usage)
    );

    return rm.findOrAdd<TextureAsset>(key, [&] {
        TextureAsset tex = makeSolidColorAsset(color, usage);
        nlohmann::json src;
        src[AssetSourceKey::KIND] = AssetSourceKind::SOLID;
        src["color"] = detail::vec4ToJson(color);
        src[AssetSourceKey::USAGE] = Reflect::enumName(usage);
        tex.sourceJson() = std::move(src);
        return tex;
    });
}

TextureHandle createGeneratedTexture(const nlohmann::json& source, ResourceManager& rm) {
    if (source.value(AssetSourceKey::KIND, std::string{}) != AssetSourceKind::SOLID) return {};

    const glm::vec4 color = detail::jsonToVec4(source.value("color", nlohmann::json()), glm::vec4(1.0f));
    return createSolidColorTexture(color, rm, textureUsageFromRecipe(source));
}

} // namespace Vkm::Engine
