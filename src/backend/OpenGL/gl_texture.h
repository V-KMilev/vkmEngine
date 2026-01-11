#pragma once

#include <memory>

namespace Core {
    class Texture2D;
}

namespace Engine {
    struct TextureAsset;
}

namespace Engine {

/**
 * @brief Encapsulates an OpenGL texture, managing GPU-side texture resources.
 *
 * GLTexture wraps Core::Texture2D and provides an engine-level interface for texture
 * management. It handles creation and updates from TextureAsset resources, ensuring
 * GPU resources are synchronized with engine asset data.
 *
 * This class is non-copyable and non-movable to ensure unique OpenGL resource ownership.
 */
class GLTexture {
    public:
        GLTexture() = delete;
        ~GLTexture();

        GLTexture(const GLTexture& other) = delete;
        GLTexture& operator=(const GLTexture& other) = delete;

        GLTexture(GLTexture && other) = delete;
        GLTexture& operator=(GLTexture && other) = delete;

        /**
         * @brief Constructs a texture from the provided asset.
         * @param texture Reference to the texture asset.
         */
        GLTexture(const TextureAsset& texture);

    public:
        /**
         * @brief Updates the texture from a new asset.
         * @param texture Reference to the new texture asset.
         */
        void update(const TextureAsset& texture);

        /**
         * @brief Bind the texture to a given texture slot.
         * @param slot Texture unit slot to bind to.
         */
        void bind(uint32_t slot) const;

        /**
         * @brief Get the underlying Core::Texture2D object.
         * @return Reference to the Core texture object.
         */
        const Core::Texture2D& getTexture() const { return *m_texture; }

    private:
        std::unique_ptr<Core::Texture2D> m_texture;
};

} // namespace Engine
