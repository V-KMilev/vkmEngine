#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "system/render/editor_render_hooks.h"

namespace Vkm::Engine {

struct EditorContext;

/**
 * @brief An interactive view of a preview: what an asset editor shows its asset in.
 *
 * Fills the space it is given with the backend's preview of the draws, rendered again when the
 * view or what is drawn changes. Drag with the left or right button to orbit, with the middle one
 * (or Shift and the left) to pan, and turn the wheel to zoom; F, or a double-click, frames the
 * draws again. A strip in its top left corner picks the backdrop and swings the light round.
 */
class PreviewViewport {
    public:
        /**
         * @brief A viewport whose renders are cached under @p key.
         *
         * @param key Its preview target's key: no thumbnail's (AssetEditors::viewportKey).
         */
        explicit PreviewViewport(uint64_t key);
        ~PreviewViewport() = default;

        PreviewViewport(const PreviewViewport& other) = delete;
        PreviewViewport& operator=(const PreviewViewport& other) = delete;

        PreviewViewport(PreviewViewport && other) = delete;
        PreviewViewport& operator=(PreviewViewport && other) = delete;

    public:
        /**
         * @brief Draw the viewport into the current window and answer the mouse over it.
         *
         * @param ec      The frame's editor context.
         * @param draws   What it shows, in its own space.
         * @param version What the draws hold, stamped: a change renders again.
         * @param size    Its size in screen pixels; a component of 0 takes the room left.
         */
        void draw(EditorContext& ec, const std::vector<PreviewDraw>& draws, uint64_t version, glm::vec2 size);

        /// Look at the draws again as a viewport first does.
        void frame();

    private:
        static constexpr float YAW      = 35.0f;  ///< The framed view's yaw, degrees.
        static constexpr float PITCH    = 20.0f;  ///< Its pitch, degrees.
        static constexpr float DISTANCE = 2.6f;   ///< Its distance, in the draws' bounding radii.

    private:
        /// The strip of backdrop and light buttons over the picture's top left corner.
        void drawStrip(glm::vec2 corner);

    private:
        uint64_t          m_key;
        float             m_yaw        = YAW;
        float             m_pitch      = PITCH;
        float             m_distance   = DISTANCE;
        glm::vec2         m_pan{0.0f};
        PreviewBackground m_background = PreviewBackground::Grey;
        float             m_lightYaw   = 0.0f;
};

} // namespace Vkm::Engine
