#pragma once

#include <cstdint>
#include <memory>

#include "gl_frame_buffer.h"

namespace Core {
    class Context;
    class Texture2D;
}

namespace Engine {

/**
 * @brief Single-channel ambient-occlusion target - one R16F texture in its own FBO.
 *
 * The GTAO pass renders the screen-space occlusion factor into this; the forward
 * pass samples it to modulate the indirect (ambient/IBL) term. Deliberately
 * lighter than GLTarget: no depth, no G-buffer, no HDR colour - just the AO
 * factor, linear-filtered so a future half-res variant can upsample cleanly.
 */
class GLAOTarget {
    public:
        GLAOTarget();
        ~GLAOTarget();

        GLAOTarget(const GLAOTarget& other) = delete;
        GLAOTarget& operator=(const GLAOTarget& other) = delete;

        GLAOTarget(GLAOTarget && other) = delete;
        GLAOTarget& operator=(GLAOTarget && other) = delete;

    public:
        void resize(uint32_t width, uint32_t height);

        /**
         * @brief Bind for rendering the AO factor (sets the FBO + viewport). Non-const:
         * it mutates GL draw-buffer state.
         */
        void bind(const Core::Context& gl);

        /**
         * @brief Bind the AO texture to @p slot for the forward pass to sample.
         *
         * @param slot Texture unit the forward shader reads the AO factor from.
         */
        void bindTexture(uint32_t slot) const;

    private:
        uint32_t m_width  = 0;
        uint32_t m_height = 0;

        Core::FrameBuffer                m_fbo;
        std::unique_ptr<Core::Texture2D> m_tex;
};

} // namespace Engine
