#pragma once

#include <memory>

#include "gl_pass.h"

namespace Vkm::GL {
    class Shader;
}

namespace Engine {

/**
 * @brief Energy-conserving bloom over the HDR scene (COD/Jimenez).
 *
 * Progressively downsamples the HDR scene into the bloom mip chain (Karis-
 * averaged, soft-knee first tap to tame fireflies), then additively upsamples
 * with a 3x3 tent. Mip 0 is left holding the final bloom, which the composite
 * pass blends in before tonemap. Runs after the HDR scene is fully resolved
 * (forward, decals, fog, DoF) and before the grid + composite.
 */
class GLBloomPass : public GLPass {
    public:
        GLBloomPass();
        ~GLBloomPass() override;

        GLBloomPass(const GLBloomPass& other) = delete;
        GLBloomPass& operator=(const GLBloomPass& other) = delete;

        GLBloomPass(GLBloomPass && other) = delete;
        GLBloomPass& operator=(GLBloomPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        std::unique_ptr<Vkm::GL::Shader> m_down;
        std::unique_ptr<Vkm::GL::Shader> m_up;
};

} // namespace Engine
