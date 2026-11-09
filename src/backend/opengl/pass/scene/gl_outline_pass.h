#pragma once

#include <vector>

#include "vkm_opengl/shader/shader.h"

#include "pass/gl_pass.h"
#include "frame/draw/gl_draw_list.h"

namespace Vkm::Engine {

/**
 * @brief The rims of the meshes an Outline asks for (RenderView::SceneLists::outlines), into the multisample
 *        scene after the opaque pass.
 *
 * Each mesh is drawn again with its surface pushed out along its normal by the outline's width in
 * pixels, its back faces alone: depth-tested, they show only past its silhouette, where nothing
 * stands in front. Through walls, the pushed-out mesh is drawn untested where the mesh itself is
 * not, which the color target's alpha marks for the pass's length. A no-op with no outline.
 */
class GLOutlinePass : public GLPass {
    public:
        GLOutlinePass();
        ~GLOutlinePass() override;

        GLOutlinePass(const GLOutlinePass& other) = delete;
        GLOutlinePass& operator=(const GLOutlinePass& other) = delete;

        GLOutlinePass(GLOutlinePass && other) = delete;
        GLOutlinePass& operator=(GLOutlinePass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        /**
         * @brief Which outlines a step draws.
         */
        enum class Which { All, ThroughWalls };

        /**
         * @brief One outlined mesh's draw: a command of its own, so each takes its own width and color.
         */
        struct Rim {
            const GLMesh* mesh;
            uint32_t      outline;   ///< Its place in RenderView::SceneLists::outlines.
            bool          skinned;   ///< Drawn through the skinned program.
            bool          mirrored;  ///< Its transform mirrors it, which turns its winding.
        };

    private:
        /**
         * @brief Draw the outlined meshes once, each pushed out by its width times @p widen, in its
         *        color times @p tint, alpha @p alpha.
         *
         * @param ctx   The frame.
         * @param which The outlines to draw.
         * @param widen 1 for the rim's mesh, 0 for the mesh itself.
         * @param tint  1 for the rim's color, 0 for black.
         * @param alpha What alpha it writes.
         */
        void drawMeshes(GLFrameContext& ctx, Which which, float widen, float tint, float alpha);

    private:
        Vkm::OpenGL::Shader m_shader;
        Vkm::OpenGL::Shader m_skinnedShader;

        std::vector<Rim> m_rims;
        GLDrawList       m_list;  ///< A command a rim, an instance each.
};

} // namespace Vkm::Engine
