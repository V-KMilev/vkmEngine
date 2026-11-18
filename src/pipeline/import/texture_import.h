#pragma once

#include <cstddef>
#include <string>

#include <nlohmann/json_fwd.hpp>

#include "resource/asset/texture_asset.h"
#include "resource/handle.h"
#include "resource/asset/texture_params.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Decode an encoded image held in memory - a PNG or JPG embedded in a
 *        model - by the rule every texture import follows (decodeChannels).
 *
 * Fills the size, the formats and the pixels; the name, the recipe, the wrap
 * and the mip policy are the caller's.
 *
 * @param bytes The encoded file.
 * @param size  Its length in bytes.
 * @param usage What the texels mean, which decides how they are stored.
 * @param out   Receives the decoded texture; untouched on failure.
 * @return False, with stbi_failure_reason() saying why, when it does not decode.
 */
bool decodeTextureFromMemory(const unsigned char* bytes, size_t size, TextureUsage usage, TextureAsset& out);

/**
 * @brief Load a texture from a file, or return the one already loaded from it (findLoadedTexture)
 *        as it was loaded: its usage, mips, filter and wrap.
 *
 * Decoded with stb_image: PNG, JPG, TGA, BMP and the rest of its formats.
 *
 * @param filePath Path to the image file.
 * @param resourceManager Resource manager to add the texture to.
 * @param usage What the texels mean - color, data or a normal - which decides
 *        how the file's channels are decoded and stored (decodeChannels).
 * @param generateMipmaps Whether to generate mipmaps.
 * @param filterOverride The texture's own say over its sampling; None leaves it
 *        to the scene's filtering setting.
 * @param wrap How sampling behaves outside [0,1]; set on both axes, and carried
 *        in the recipe so a texture rebuilt from it tiles the same way.
 * @return Handle to the loaded texture, or invalid handle on failure.
 */
TextureHandle loadTexture(
    const std::string& filePath,
    ResourceManager& resourceManager,
    TextureUsage usage = TextureUsage::Data,
    bool generateMipmaps = true,
    TextureFilterOverride filterOverride = TextureFilterOverride::None,
    TextureWrapMode wrap = TextureWrapMode::ClampToEdge
);

/**
 * @brief The texture already loaded from @p filePath, if any.
 *
 * A texture's identity is its path: one asked for as another usage is returned as it was
 * loaded, and the ask reported, since decoding it again would change it under everything
 * that uses it.
 *
 * @param filePath Path to the image file, or the texture's name.
 * @param resourceManager Where it would be loaded.
 * @param usage How the asker means to read it.
 * @return Its handle, or an invalid one when it is not loaded.
 */
TextureHandle findLoadedTexture(
    const std::string& filePath,
    const ResourceManager& resourceManager,
    TextureUsage usage
);

/**
 * @brief Import a texture without blocking, finalized on a later frame.
 *
 * Returns immediately with a valid handle; the asset starts in a `loading`
 * state with no pixel data and is finalized by AsyncLoaderSystem on a later
 * frame. Until finalized the texture has no pixels, so the renderer substitutes its
 * missing-texture checker rather than sampling unfilled
 * storage. For critical visuals where a pop-in is unacceptable, use synchronous
 * loadTexture().
 *
 * Idempotent: requesting the same path twice (within a session) returns the
 * same handle - findByName(path) dedup at the resource layer.
 *
 * @param filePath Path to the image file to import.
 * @param resourceManager Resource manager the stub texture is added to.
 * @param usage What the texels mean - color, data or a normal - which decides
 *        how the file's channels are decoded and stored (decodeChannels).
 * @param generateMipmaps Whether to generate mipmaps once the pixels are decoded.
 * @param filterOverride The texture's own say over its sampling; None leaves it
 *        to the scene's filtering setting.
 * @param wrap How sampling behaves outside [0,1]; set on both axes, and carried
 *        in the recipe so a texture rebuilt from it tiles the same way.
 * @return Handle to the loading texture; valid immediately, filled on a later frame.
 */
TextureHandle requestTextureAsync(
    const std::string& filePath,
    ResourceManager& resourceManager,
    TextureUsage usage = TextureUsage::Data,
    bool generateMipmaps = true,
    TextureFilterOverride filterOverride = TextureFilterOverride::None,
    TextureWrapMode wrap = TextureWrapMode::ClampToEdge
);

} // namespace Vkm::Engine
