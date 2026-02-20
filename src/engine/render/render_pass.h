#pragma once

#include <cstdint>
#include <string>

namespace Engine {
    class RenderBackend;

    class RenderView;
    class ResourceManager;
}

namespace Engine {

/**
 * @brief Abstract base class for an individual rendering pass.
 *
 * Each RenderPass is uniquely named and defines a specific action to be performed
 * during frame rendering (e.g., rendering geometry, drawing UI, post-processing).
 * Subclasses must implement onResize and execute to describe how the pass responds
 * to framebuffer changes and per-frame rendering logic.
 *
 * RenderPasses are designed to be non-copyable and non-movable, ensuring each pass
 * maintains unique state and identity.
 */
class RenderPass {
    public:
        RenderPass() = delete;
        virtual ~RenderPass() = default;

        RenderPass(const RenderPass& other) = delete;
        RenderPass& operator=(const RenderPass& other) = delete;

        RenderPass(RenderPass && other) = delete;
        RenderPass& operator=(RenderPass && other) = delete;

        /**
         * @brief Construct a named render pass.
         * @param name A human-friendly identifier for this pass (e.g., "Geometry", "Shadow", etc)
         */
        RenderPass(const std::string& name) : m_name(name) {}

    public:
        /**
         * @brief Get the human-friendly name of this render pass.
         * @return Reference to name string.
         */
        const std::string& getName() const { return m_name; }

        bool isEnabled() const { return m_enabled; }
        void setEnabled(bool enabled) { m_enabled = enabled; }

        /**
         * @brief Respond to framebuffer or window resizing by adapting the pass.
         *
         * Should be called when the output target size changes, giving the pass
         * a chance to reallocate or update relevant resources.
         *
         * @param backend Reference to the current RenderBackend; may be downcast if needed.
         * @param width New framebuffer (or window) width, in pixels.
         * @param height New framebuffer (or window) height, in pixels.
         */
        virtual void onResize(RenderBackend& backend, uint32_t width, uint32_t height) = 0;

        /**
         * @brief Execute the core rendering logic for this pass.
         *
         * Called every frame; performs the intended GPU operation(s) described by this pass,
         * using the provided backend and scene data.
         *
         * @param backend Reference to the current RenderBackend (OpenGL, Optix, CPU, etc).
         * @param view    Scene and camera data needed for rendering.
         * @param resources Access to all GPU resource handles for this frame.
         */
        virtual void execute(RenderBackend& backend, const RenderView& view, const ResourceManager& resources) = 0;

    protected:
        std::string m_name;
        bool m_enabled = true;
};

} // namespace Engine
