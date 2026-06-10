#pragma once

#include <cstdint>
#include <memory>

namespace Core {
    class Texture2D;
}

namespace Engine {

struct TextureAsset;

/**
 * @brief GPU copy of a 2D texture asset (wraps Core::Texture2D).
 */
class GLTexture {
    public:
        explicit GLTexture(const TextureAsset& texture);
        ~GLTexture();

        GLTexture(const GLTexture& other) = delete;
        GLTexture& operator=(const GLTexture& other) = delete;

        GLTexture(GLTexture && other) = delete;
        GLTexture& operator=(GLTexture && other) = delete;

    public:
        void update(const TextureAsset& texture);
        void bindSlot(uint32_t slot) const;

        const Core::Texture2D& getTexture() const { return *m_texture; }

    private:
        std::unique_ptr<Core::Texture2D> m_texture;
};

} // namespace Engine
