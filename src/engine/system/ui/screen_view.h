#pragma once

#include <optional>

#include <glm/glm.hpp>

#include "core/math/bounds.h"

namespace Vkm::Engine {

class HostChrome;
class Scene;
class WindowManager;
struct UICanvas;

/**
 * @brief The 3D view a game draws through: where it sits in the window, and the scene's active
 *        camera filling it.
 *
 * What a game needs to turn the pointer into a ray into the world, or a point of the world into a
 * place on a canvas, the way the renderer and the UI themselves see them. The view is the
 * HostChrome's rect, not the window: in the editor's Play panel the two differ, and a game that
 * divided by the window would pick beside the cursor and draw its labels off their targets. A
 * host's own view (HostView) is the host's, and not asked.
 */
class ScreenView {
    public:
        ScreenView()  = default;
        ~ScreenView() = default;

        ScreenView(const ScreenView& other) = default;
        ScreenView& operator=(const ScreenView& other) = default;

        ScreenView(ScreenView && other) = default;
        ScreenView& operator=(ScreenView && other) = default;

    public:
        /**
         * @brief The view as it stands now.
         *
         * @param scene  Where the active camera is (findActiveCamera).
         * @param window The window drawn into; its framebuffer scale converts the pointer.
         * @param chrome The host's rect for the scene inside it.
         * @return A view; valid() is false with no camera or an empty rect.
         */
        static ScreenView of(const Scene& scene, const WindowManager& window, const HostChrome& chrome);

        bool valid() const { return m_valid; }

        /**
         * @brief The ray from the camera through a point of the window.
         *
         * @param windowPoint In window pixels, as InputMap::pointer() reports the cursor.
         * @return The ray, its origin on the near plane; nothing when the point is outside the view
         *         or there is no view.
         */
        std::optional<Math::Ray> rayThrough(const glm::vec2& windowPoint) const;

        /**
         * @brief Where a point of the world lands on a canvas, in the canvas's own pixels.
         *
         * Those are the units a UIElement inside it is placed in, measured from the canvas's top
         * left, so an element anchored there at this offset sits over the point.
         *
         * @param world  The point.
         * @param canvas The canvas, whose scaling the answer is in.
         * @return The place; nothing for a point behind the camera, or with no view.
         */
        std::optional<glm::vec2> toCanvas(const glm::vec3& world, const UICanvas& canvas) const;

    private:
        bool      m_valid = false;
        glm::mat4 m_viewProjection{1.0f};
        glm::vec2 m_origin{0.0f};  ///< The view's top left in the framebuffer, pixels.
        glm::vec2 m_size{0.0f};    ///< Its size, framebuffer pixels.
        float     m_pointerScale = 1.0f;  ///< Framebuffer pixels per window pixel.
};

} // namespace Vkm::Engine
