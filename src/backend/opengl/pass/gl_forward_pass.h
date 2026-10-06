#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "gl_shader.h"

#include "gl_pass.h"
#include "frame/gl_instance_batcher.h"

namespace Vkm::Engine {

/**
 * @brief The lit forward draw of the scene's geometry.
 *
 * Three buckets in order: Opaque/Unlit against GLDepthPrepass's depth (LEQUAL, writes off),
 * AlphaMask priming its own, then Transparent back-to-front. Never clears; back faces are culled
 * (materials are single-sided). Each surface fogs at its own depth (see GLPass::bindFog).
 *
 * Skinned runs sort after static ones, so a bucket switches program once. One place sets both
 * programs' uniforms: one set on only one would silently go missing on characters.
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
        /// Which of the three buckets a set of frame uniforms is for.
        enum class Bucket {
            Opaque,       ///< Opaque and unlit, against the primed depth.
            AlphaMask,    ///< Alpha-masked, priming its own depth.
            Transparent,  ///< Blended back-to-front over the refraction grab.
        };

        /**
         * @brief Submit a batch's draws.
         *
         * Switches program at the skinned boundary and rebinds material state only when the
         * material changes; each draw is one multi-draw. Material bindings are context state, so
         * they survive the program switch.
         *
         * @param ctx   For the GPU mirror and the context.
         * @param batch The draws to submit.
         */
        void drawBatch(GLFrameContext& ctx, const GLInstanceBatcher& batch);

        /**
         * @brief The Wireframe view's lines: the opaque and alpha-masked draws again, as lines.
         *
         * Blended over what they shaded, pulled toward the camera so the surface they lie on
         * does not hide them, and cut where the cutout cut. Transparent surfaces draw none.
         *
         * @param ctx   For the batches and the context.
         * @param posed Whether the skinned program is in use this frame.
         */
        void drawWireframe(GLFrameContext& ctx, bool posed);

        /**
         * @brief Bind @p shader and give it this frame's uniforms.
         *
         * Once per program per frame, as uniform state is per program. The textures bindAmbient
         * and bindFog bind are context state, bound again for the second program.
         *
         * @param shader Program to bind and fill.
         * @param ctx    For the settings and pass products.
         */
        void bindFrameUniforms(Vkm::GL::Shader& shader, GLFrameContext& ctx) const;

        /**
         * @brief Bind @p shader and tell it which bucket draws next.
         *
         * Only the opaque bucket is in the prepass GTAO reads, so only it is told it has AO;
         * elsewhere the texel is the surface behind, or the sky. The refraction grab is live only
         * for Transparent, drawn after the copy; alpha-to-coverage only for AlphaMask on a
         * multisample target, else the shader cuts at half coverage.
         *
         * @param shader Program to bind and fill.
         * @param ctx    For the pass products and the target.
         * @param bucket The bucket about to draw with it.
         */
        void bindBucketUniforms(Vkm::GL::Shader& shader, GLFrameContext& ctx, Bucket bucket) const;

    private:
        Vkm::GL::Shader   m_shader;         ///< Static geometry.
        Vkm::GL::Shader   m_skinnedShader;  ///< Skinned, posed by the frame's palette.
        GLInstanceBatcher m_batcher;

        // Back-to-front, refilled each frame with capacity kept. The opaque bucket is the context's.
        std::vector<std::pair<float, uint32_t>> m_transparent;
        std::vector<uint32_t>                   m_transparentSorted;
};

} // namespace Vkm::Engine
