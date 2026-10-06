#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "resource/texture_format.h"
#include "system/render/render_settings.h"
#include "gl_texture.h"

namespace Vkm::Engine {

struct TextureAsset;
struct FontAsset;

/**
 * @brief GPU copy of a 2D texture-shaped asset (wraps Vkm::GL::Texture2D).
 *
 * Named for the asset because `gl_texture.h` is vkmGL's, and a flat include
 * cannot tell two headers of one name apart.
 *
 * Uploads either a TextureAsset or a FontAsset's SDF atlas (fonts carry their
 * atlas as raw pixels, not as a TextureAsset), so GLView can table both behind
 * the same GL resource type.
 */
class GLTexture {
    public:
        explicit GLTexture(const TextureAsset& texture);
        explicit GLTexture(const FontAsset& font);
        ~GLTexture();

        GLTexture(const GLTexture& other) = delete;
        GLTexture& operator=(const GLTexture& other) = delete;

        GLTexture(GLTexture && other) = delete;
        GLTexture& operator=(GLTexture && other) = delete;

    public:
        void update(const TextureAsset& texture);
        void update(const FontAsset& font);

        /**
         * @brief Re-filter this texture for a frame drawn at @p sceneMode.
         *
         * The asset's own filter override is kept here from the last upload, so
         * this is where the two claims on a texture's filtering meet and only
         * one of them can come out: resolveTextureFilter gives the asset the
         * say wherever it stated one and the setting the rest of the table.
         *
         * Forwarded rather than reached through getTexture() so the GL object
         * itself stays const to everything downstream.
         *
         * @param sceneMode     RenderSettings::textureFiltering for this frame.
         * @param maxAnisotropy Requested degree; the GL layer clamps it.
         */
        void applyFiltering(TextureFiltering sceneMode, float maxAnisotropy);

        const Vkm::GL::Texture2D& getTexture() const { return *m_texture; }

        /**
         * @brief Whether real pixels have ever been uploaded into this texture.
         *
         * False for an asset that is still streaming in or whose decode failed:
         * the GL object exists and is bindable, but its contents are undefined.
         * Callers should substitute the missing-texture placeholder rather than
         * sample it, so a load failure is visible instead of arbitrary.
         */
        bool hasPixels() const { return m_hasPixels; }

        /**
         * @brief Which upload this texture's pixels came from, distinct across every GLTexture.
         *
         * Changes whenever pixels arrive: the handle names the asset, not the
         * pixels, so a cache keyed on the handle alone misses a re-upload.
         */
        uint64_t uploadId() const { return m_uploadId; }

    private:
        std::unique_ptr<Vkm::GL::Texture2D> m_texture;
        uint64_t                            m_uploadId = 0;

        /// The asset's half of its filtering, kept from the last upload.
        TextureFilterOverride m_filterOverride = TextureFilterOverride::None;
        bool                  m_mipmapped      = false;

        bool m_hasPixels = false;
};

/**
 * @brief Decode the image at @p path and upload it as sRGB colour with no mip chain.
 *
 * For a picture the host shows at about its own size and no asset describes,
 * so it is read straight off disk. Rows arrive bottom-up (see decodeImageRGBA);
 * a drawer whose UVs run the other way flips them at the draw.
 *
 * @param path Image file to read.
 * @return The texture, or null when the file does not decode.
 */
std::unique_ptr<Vkm::GL::Texture2D> uploadImageFile(const std::string& path);

} // namespace Vkm::Engine
