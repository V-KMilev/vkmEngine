#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "vkm_opengl/texture/texture.h"

#include "resource/asset/texture_params.h"

namespace Vkm::Engine {

struct TextureAsset;
struct FontAsset;
struct DecodedImage;

/**
 * @brief GPU copy of a 2D texture-shaped asset (wraps Vkm::OpenGL::Texture2D).
 *
 * Uploads either a TextureAsset or a FontAsset's SDF atlas (fonts carry their
 * atlas as raw pixels, not as a TextureAsset), so GLAssetMirror can table both behind
 * the same GL resource type.
 */
class GLAssetTexture {
    public:
        explicit GLAssetTexture(const TextureAsset& texture);
        explicit GLAssetTexture(const FontAsset& font);
        ~GLAssetTexture();

        GLAssetTexture(const GLAssetTexture& other) = delete;
        GLAssetTexture& operator=(const GLAssetTexture& other) = delete;

        GLAssetTexture(GLAssetTexture && other) = delete;
        GLAssetTexture& operator=(GLAssetTexture && other) = delete;

    public:
        void update(const TextureAsset& texture);
        void update(const FontAsset& font);

        /**
         * @brief Re-filter this texture at the frame's anisotropy.
         *
         * The asset's own filter override is kept here from the last upload, so
         * this is where the two claims on a texture's filtering meet and only
         * one of them can come out: resolveTextureFilter gives the asset the
         * say wherever it stated one and the setting the rest of the table.
         *
         * Forwarded rather than reached through getTexture() so the GL object
         * itself stays const to everything downstream.
         *
         * @param maxAnisotropy Requested degree; the GL layer clamps it.
         */
        void applyFiltering(float maxAnisotropy);

        const Vkm::OpenGL::Texture2D& getTexture() const { return *m_texture; }

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
         * @brief Which upload this texture's pixels came from, distinct across every GLAssetTexture.
         *
         * Changes whenever pixels arrive: the handle names the asset, not the
         * pixels, so a cache keyed on the handle alone misses a re-upload.
         */
        uint64_t uploadId() const { return m_uploadId; }

    private:
        std::unique_ptr<Vkm::OpenGL::Texture2D> m_texture;
        uint64_t                                m_uploadId = 0;

        TextureFilterOverride m_filterOverride = TextureFilterOverride::None;
            ///< The asset's half of its filtering, kept from the last upload.
        bool                  m_mipmapped      = false;
        bool                  m_color          = true;  ///< Whether it holds color, not normals or data.

        bool m_hasPixels = false;
};

} // namespace Vkm::Engine
