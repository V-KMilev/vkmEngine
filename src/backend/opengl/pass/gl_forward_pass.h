#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "gl_pass.h"
#include "data/gl_instance_batcher.h"

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
 * draw first, then Transparent draws back-to-front with alpha blending and
 * depth writes off. With the depth prepass in front (the standard pipeline) the
 * opaque draw runs LEQUAL with depth writes off for early-Z; in the no-prepass
 * fallback it owns the depth/colour clear and writes depth itself. Back faces
 * are culled (all materials are single-sided). The camera and light UBOs are
 * uploaded by the backend before this pass runs.
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
         * @brief Draw a list of instanced runs.
         *
         * Rebinds the material UBO + textures only when the material changes
         * between consecutive runs; each run is one instanced draw.
         */
        void drawRuns(GLFrameContext& ctx, const GLInstanceBatchView& batch);

    private:
        std::unique_ptr<Core::Shader> m_shader;
        GLInstanceBatcher             m_batcher;

        // Transparent bucket sorted back-to-front - cleared + refilled each
        // frame, capacity kept. The opaque bucket comes from the frame context.
        std::vector<std::pair<float, const DrawableData*>>  m_transparent;
        std::vector<const DrawableData*>                    m_transparentSorted;
};

} // namespace Engine
