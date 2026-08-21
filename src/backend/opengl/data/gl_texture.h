#pragma once

#include <cstdint>
#include <memory>

#include "resource/texture_format.h"
#include "system/render/render_settings.h"
#include "texture/gl_texture.h"

namespace Vkm::Engine {

struct TextureAsset;
struct FontAsset;

/**
 * @brief GPU copy of a 2D texture-shaped asset (wraps Vkm::GL::Texture2D).
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

        const Vkm::GL::Texture2D& getTexture() const { return *m_texture; }

        /**
         * @brief Re-filter this texture for a frame drawn at @p sceneMode.
         *
         * The asset's own filter override is kept here from the last upload, so
         * this is where the two claims on a texture's filtering meet and only
         * one of them can come out: resolveTextureFilter gives the asset the
         * say wherever it stated one and the setting the rest of the table.
         *
         * Forwarded rather than reached through getTexture() so the GL object
         * itself stays const to everything downstream: the passes bind it, and
         * filtering is all the backend changes about it after upload.
         *
         * @param sceneMode     RenderSettings::textureFiltering for this frame.
         * @param maxAnisotropy Requested degree; the GL layer clamps it.
         */
        void applyFiltering(TextureFiltering sceneMode, float maxAnisotropy);

        /**
         * @brief Whether real pixels have ever been uploaded into this texture.
         *
         * False for an asset that is still streaming in or whose decode failed:
         * the GL object exists and is bindable, but its contents are undefined.
         * Callers should substitute the missing-texture placeholder rather than
         * sample it, so a load failure is visible instead of arbitrary.
         */
        bool hasPixels() const { return m_hasPixels; }

    private:
        std::unique_ptr<Vkm::GL::Texture2D> m_texture;

        /// The asset's half of its filtering, kept from the last upload.
        TextureFilterOverride m_filterOverride = TextureFilterOverride::None;
        bool                  m_mipmapped      = false;

        bool m_hasPixels = false;
};

} // namespace Vkm::Engine
