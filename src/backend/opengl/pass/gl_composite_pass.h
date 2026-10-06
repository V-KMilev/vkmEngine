#pragma once

#include "gl_shader.h"

#include "gl_pass.h"

namespace Vkm::Engine {

/**
 * @brief Resolves the HDR scene target to the backbuffer.
 *
 * Tonemaps and sRGB-encodes (`linearToSrgb`, `shaders/color.glsl`) the HDR target into the
 * default framebuffer. The backbuffer is plain 8-bit with no sRGB conversion of its own, so the
 * encode, the dither and the blending of everything drawn later happen in display values.
 */
class GLCompositePass : public GLPass {
    public:
        GLCompositePass();
        ~GLCompositePass() override;

        GLCompositePass(const GLCompositePass& other) = delete;
        GLCompositePass& operator=(const GLCompositePass& other) = delete;

        GLCompositePass(GLCompositePass && other) = delete;
        GLCompositePass& operator=(GLCompositePass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        Vkm::GL::Shader m_shader;
};

} // namespace Vkm::Engine
