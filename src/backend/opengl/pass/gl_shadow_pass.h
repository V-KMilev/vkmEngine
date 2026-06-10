#pragma once

#include <memory>

#include <glm/glm.hpp>

#include "gl_pass.h"

namespace Core {
    class Shader;
}

namespace Engine {

/**
 * @brief Renders all shadow depth maps for the frame, ahead of the forward pass.
 *
 * Consumes the plan in GLShadowData (built by the backend): directional cascades
 * and spot maps go into the 2D depth atlas, point lights into cube maps. A
 * single depth-only draw of every castShadows drawable feeds each tile / face.
 * A no-op when the frame has no shadow casters.
 */
class GLShadowPass : public GLPass {
    public:
        GLShadowPass();
        ~GLShadowPass() override;

        GLShadowPass(const GLShadowPass& other) = delete;
        GLShadowPass& operator=(const GLShadowPass& other) = delete;

        GLShadowPass(GLShadowPass && other) = delete;
        GLShadowPass& operator=(GLShadowPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        /**
         * @brief Fill the 2D depth atlas for directional cascades and spots.
         *
         * Clears the atlas once, then rasterises every shadow caster into each
         * cascade / spot tile with the projected-depth shader. A no-op when the
         * frame has no 2D casters.
         */
        void render2D(GLFrameContext& ctx);

        /**
         * @brief Fill each point light's depth cube.
         *
         * Six faces per light, rendered with the distance-depth shader so the
         * forward pass can sample the cube by direction.
         */
        void renderCube(GLFrameContext& ctx);

        /**
         * @brief Draw the scene's shadow casters that fall inside @p lightVP.
         *
         * Frustum-culls RenderView::shadowCasters against @p lightVP (the
         * caster set is scene-wide, so off-screen occluders still cast) and
         * draws the survivors with @p shader, setting u_model per draw. The
         * caller has already bound @p shader and set its view matrix.
         *
         * @param shader  The bound depth shader to issue the draws against.
         * @param lightVP The light clip-space matrix to cull and draw against.
         */
        void renderCasters(GLFrameContext& ctx, Core::Shader& shader, const glm::mat4& lightVP);

    private:
        std::unique_ptr<Core::Shader> m_depth2D;    ///< Projected depth (cascades + spots).
        std::unique_ptr<Core::Shader> m_depthCube;  ///< Linear distance depth (point faces).
};

} // namespace Engine
