#pragma once

#include "gl_pass.h"

namespace Vkm::Engine {

/**
 * @brief Draws what water's pixels read besides the OceanBlock: the ripples (GLOceanFields), which
 * the sea and every other water show, and the sea's shore (GLOceanShore).
 *
 * Before the mirrors and the forward pass, which both bind them on their units (OceanTextureSlots).
 * The ripples are drawn while the scene has an ocean or other water is drawn, and the wind raises
 * any (GLOcean::rippling); the shore only for an ocean with waterline foam, seen from a camera.
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
