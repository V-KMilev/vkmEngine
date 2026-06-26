#pragma once

#include <memory>

#include "gl_pass.h"
#include "gl_screen_triangle.h"

namespace Core {
    class Shader;
}

namespace Engine {

/**
 * @brief Screen-space reflections composited over the HDR scene.
 *
 * Snapshots the scene colour + depth, reconstructs view-space position and a
 * geometric normal from depth, ray-marches the reflected view ray against the
 * depth copy, and composites the hit's scene colour (Fresnel + edge faded) into
 * a scratch target that is then blitted back over the HDR target. Runs after the
 * forward draw and before bloom, so reflections bloom and tonemap with the rest
 * of the scene.
 */
class GLSSRPass : public GLPass {
    public:
        GLSSRPass();
        ~GLSSRPass() override;

        GLSSRPass(const GLSSRPass& other) = delete;
        GLSSRPass& operator=(const GLSSRPass& other) = delete;

        GLSSRPass(GLSSRPass && other) = delete;
        GLSSRPass& operator=(GLSSRPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        std::unique_ptr<Core::Shader> m_shader;
        Core::ScreenTriangle          m_tri;
};

} // namespace Engine
