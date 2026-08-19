#pragma once

#include <cstdint>
#include <memory>

namespace Vkm::GL {
    class Texture2D;
}

namespace Engine {

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
        bool                              m_hasPixels = false;
};

} // namespace Engine
