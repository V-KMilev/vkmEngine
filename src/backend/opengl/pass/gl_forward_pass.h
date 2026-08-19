#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "gl_pass.h"
#include "data/gl_instance_batcher.h"

namespace Vkm::GL {
    class Shader;
}

namespace Vkm::Engine {
    struct DrawableData;
}

namespace Vkm::Engine {

/**
 * @brief The lit forward draw - the one geometry pass the backend runs.
 *
 * Draws the three buckets the backend partitioned, in order: Opaque / Unlit
 * against the depth the prepass primed (LEQUAL, writes off, for early-Z), then
 * AlphaMask, which primes its own, then Transparent back-to-front. The prepass
 * is unconditional and owns the clear, so this pass never clears. Back faces
 * are culled (all materials are single-sided). The camera and light UBOs are
 * uploaded by the backend before this pass runs.
 *
 * Two programs, differing only in their vertex stage: skinned runs lead each
 * batch, so a bucket switches once. They share every per-frame uniform, which is
 * why one place sets both - uniform state is per program in GL, and a uniform
 * added to only one of them would go silently missing on characters.
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
         * Switches program at the skinned boundary and rebinds the material UBO
         * + textures only when the material changes between consecutive runs;
         * each run is one instanced draw. The material bindings are context
         * state rather than program state, so they survive the program switch.
         */
        void drawRuns(GLFrameContext& ctx, const GLInstanceBatchView& batch);

        /**
         * @brief Bind @p shader and give it this frame's uniforms.
         *
         * Called once per program per frame. The textures and UBOs the pass
         * binds are context state and are set once in execute(); everything
         * here is program state and has to be set on each of them.
         *
         * @param shader        Program to bind and fill.
         * @param ctx           The frame, for the settings and pass products.
         * @param hasSceneColor Whether the refraction grab is live - true only
         *                      for the transparent bucket, which draws after
         *                      the opaque scene has been copied.
         */
        void bindFrameUniforms(Vkm::GL::Shader& shader, GLFrameContext& ctx,
                               bool hasSceneColor) const;

    private:
        std::unique_ptr<Vkm::GL::Shader> m_shader;         ///< Static geometry.
        std::unique_ptr<Vkm::GL::Shader> m_skinnedShader;  ///< Same shading, with the vertices posed by the frame's palette.
        GLInstanceBatcher              m_batcher;

        // Sorted back-to-front, cleared + refilled each frame with the capacity
        // kept. The opaque bucket comes from the frame context.
        std::vector<std::pair<float, const DrawableData*>>  m_transparent;
        std::vector<const DrawableData*>                    m_transparentSorted;
};

} // namespace Vkm::Engine
