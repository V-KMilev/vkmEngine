#pragma once

#include <glm/glm.hpp>

#include "asset_editors/asset_editor.h"

namespace Vkm::Engine {

/**
 * @brief A texture at any zoom, over a checkerboard its transparency shows against, beside what
 *        it is stored as.
 *
 * The wheel zooms about the cursor, a drag pans, and a double-click fits it to the window again;
 * the channel buttons show its red, green and blue apart.
 */
class TextureViewer : public AssetEditor {
    public:
        /**
         * @brief A viewer of the texture at @p key.
         *
         * @param key Its handle's slot and generation.
         */
        explicit TextureViewer(StorageIndex key);
        ~TextureViewer() override = default;

        TextureViewer(const TextureViewer& other) = delete;
        TextureViewer& operator=(const TextureViewer& other) = delete;

        TextureViewer(TextureViewer && other) = delete;
        TextureViewer& operator=(TextureViewer && other) = delete;

    public:
        const Resource* asset(const ResourceManager& resources) const override;
        void            draw(EditorContext& ec) override;

    private:
        /// The picture: checkerboard, the texture at its zoom, the mouse answered.
        void drawPicture(EditorContext& ec, glm::vec2 room);

        /// The facts down the side, and the channel buttons.
        void drawDetails(EditorContext& ec);

    private:
        float     m_zoom = 0.0f;  ///< Screen coordinates a texel; 0 fits the texture to the picture.
        glm::vec2 m_offset{0.0f};  ///< The texture's centre from the picture's, screen coordinates.
        bool      m_channels[3] = {true, true, true};  ///< Red, green and blue shown.
};

} // namespace Vkm::Engine
