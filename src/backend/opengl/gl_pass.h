#pragma once

namespace Engine {
    struct GLFrameContext;
}

namespace Engine {

/**
 * @brief One step of the OpenGL backend's frame.
 *
 * The backend owns an ordered list of these and runs them per frame. Kept
 * deliberately small: a pass reads what it needs from the per-frame
 * GLFrameContext (scene snapshot, GPU resource mirror, render targets) and
 * issues its draws - an ordered list, not a render graph.
 */
class GLPass {
    public:
        GLPass() = default;
        virtual ~GLPass() = default;

        GLPass(const GLPass& other) = delete;
        GLPass& operator=(const GLPass& other) = delete;

        GLPass(GLPass&& other) = delete;
        GLPass& operator=(GLPass&& other) = delete;

    public:
        void setEnabled(bool enabled) { m_enabled = enabled; }
        bool isEnabled() const { return m_enabled; }

        virtual void execute(GLFrameContext& ctx) = 0;

    private:
        bool m_enabled = true;
};

} // namespace Engine
