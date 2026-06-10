#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "gl_pass.h"

namespace Core {
    class Shader;
}

namespace Engine {
    struct DrawableData;
}

namespace Engine {

/**
 * @brief The lit forward draw - the one geometry pass the backend runs.
 *
 * Splits the frame's drawables by material type: Opaque / AlphaMask / Unlit
 * draw front-to-back-agnostic with depth writes, then Transparent draws
 * back-to-front with alpha blending and depth writes off. Per-material cull
 * state honours doubleSided. The camera and light UBOs are uploaded by the
 * backend before this pass runs.
 */
class GLForwardPass : public GLPass {
    public:
        GLForwardPass();
        ~GLForwardPass() override;

        GLForwardPass(const GLForwardPass& other) = delete;
        GLForwardPass& operator=(const GLForwardPass& other) = delete;

        GLForwardPass(GLForwardPass && other) = delete;
        GLForwardPass& operator=(GLForwardPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        /**
         * @brief Draw a list of drawables in order.
         *
         * Rebinds the material UBO, textures and cull state only when the
         * material changes between consecutive draws; sets u_model per draw.
         *
         * @param list Drawables to render, pre-sorted by the caller.
         */
        void drawList(GLFrameContext& ctx, const std::vector<const DrawableData*>& list);

    private:
        std::unique_ptr<Core::Shader> m_shader;

        // Per-frame buckets - cleared and refilled each frame, capacity kept.
        std::vector<const DrawableData*>                    m_opaque;
        std::vector<std::pair<float, const DrawableData*>>  m_transparent;  ///< (view distance^2, drawable)
        std::vector<const DrawableData*>                    m_transparentSorted;
};

} // namespace Engine
