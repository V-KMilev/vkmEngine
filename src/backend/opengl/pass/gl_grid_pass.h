#pragma once

#include "gl_shader.h"

#include "gl_pass.h"

namespace Vkm::Engine {

/**
 * @brief Draws the editor's world grid: grids on the XZ, XY and ZY planes, and the axis lines.
 *
 * One fullscreen draw after Composite, so it blends in display space and the axes keep
 * Math::AXIS_COLORS as the gizmos show them. Each pixel finds its ray's point on each plane
 * and its nearest point on each axis, and tests them against the geometry target's depth.
 * Drawn while RenderSettings::gridShown: a line for each axis on, and the plane of each
 * two; an orthographic view down an axis draws the plane facing it instead.
 */
class GLGridPass : public GLPass {
    public:
        GLGridPass();
        ~GLGridPass() override;

        GLGridPass(const GLGridPass& other) = delete;
        GLGridPass& operator=(const GLGridPass& other) = delete;

        GLGridPass(GLGridPass && other) = delete;
        GLGridPass& operator=(GLGridPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        Vkm::GL::Shader m_shader;
};

} // namespace Vkm::Engine
