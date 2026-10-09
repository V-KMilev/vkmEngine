#pragma once

#include "gl_pass.h"

namespace Vkm::Engine {

/**
 * @brief Draws what a `waves` surface's pixels read besides the OceanBlock (GLOceanFields): the
 * ocean's ripples, and its shore.
 *
 * Just before the forward pass, which binds them on their units (OceanTextureSlots). A scene with
 * no ocean draws neither; an ocean with no ripples or no waterline foam skips that one.
 */
class GLOceanPass : public GLPass {
    public:
        GLOceanPass() = default;
        ~GLOceanPass() override = default;

        GLOceanPass(const GLOceanPass& other) = delete;
        GLOceanPass& operator=(const GLOceanPass& other) = delete;

        GLOceanPass(GLOceanPass && other) = delete;
        GLOceanPass& operator=(GLOceanPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;
};

} // namespace Vkm::Engine
