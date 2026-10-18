#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "gl_shader.h"

#include "gl_pass.h"
#include "frame/gl_instance_batcher.h"

namespace Vkm::Engine {

class GLMaterial;

/**
 * @brief The lit forward draw of the scene's geometry.
 *
 * Three buckets in order: Opaque/Unlit against GLDepthPrepass's depth (GLDepth::EYE_TEST_EQUAL,
 * writes off), AlphaMask priming its own, then Transparent back-to-front. Never clears; back faces
 * are culled, except for a MaterialAsset::doubleSided material: an opaque one draws unculled, a
 * transparent one twice, back faces first. Each surface fogs at its own depth (see GLPass::bindFog).
 *
 * Five programs: static and skinned, each whole and plain - compiled without the optional
 * features a GLMaterial::plain material never reads (shaders/forward/pbr_plain) - and static for a
 * GLMaterial::layered one (shaders/forward/pbr_layered). Skinned runs sort after static ones;
 * within each the program follows the material. One place sets every
 * program's uniforms (forEachProgram): one set on only some would silently go missing on whatever
 * draws with the rest.
 */
class GLForwardPass : public GLPass {
    public:
        /**
         * @brief Draws the static runs of whole and of layered materials with @p pbr and
         *        @p layered, which it borrows.
         *
         * @param pbr     The scene capture's program (GLSceneCapture::pbrProgram), whose uniforms are
         *                the last caller's: the pass sets every one it reads each frame.
         * @param layered The scene capture's layered program (GLSceneCapture::layeredProgram), the
         *                same.
         */
        GLForwardPass(Vkm::GL::Shader& pbr, Vkm::GL::Shader& layered);
        ~GLForwardPass() override;

        GLForwardPass(const GLForwardPass& other) = delete;
        GLForwardPass& operator=(const GLForwardPass& other) = delete;

        GLForwardPass(GLForwardPass && other) = delete;
        GLForwardPass& operator=(GLForwardPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

        /**
         * @brief Whether a run of @p material draws with the plain program.
         *
         * @param material The run's material; null takes the whole program, which reads every
         *                 feature's default.
         * @return True for a GLMaterial::plain material.
         */
        static bool drawsPlain(const GLMaterial* material);

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
         * Switches program where skinning or the material's plain() changes, and rebinds material
         * state only when the material changes; each draw is one multi-draw. Material bindings are
         * context state, so they survive the program switch.
         *
         * @param ctx     For the GPU mirror and the context.
         * @param batch   The draws to submit.
         * @param blended Whether the draws blend, so a double-sided one draws its far side first.
         */
        void drawBatch(GLFrameContext& ctx, const GLInstanceBatcher& batch, bool blended = false);

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
         * @brief Give @p shader this frame's uniforms.
         *
         * Once per program per frame, as uniform state is per program. The textures bindAmbient
         * and bindFog bind are context state, bound again for each program.
         *
         * @param shader Program to fill.
         * @param ctx    For the settings and pass products.
         */
        void bindFrameUniforms(Vkm::GL::Shader& shader, GLFrameContext& ctx) const;

        /**
         * @brief Tell @p shader which bucket draws next.
         *
         * Only the opaque bucket is in the prepass GTAO reads, so only it is told it has AO;
         * elsewhere the texel is the surface behind, or the sky. The refraction grab is live only
         * for Transparent, drawn after the copy; alpha-to-coverage only for AlphaMask on a
         * multisample target, else the shader cuts at half coverage.
         *
         * @param shader Program to fill.
         * @param ctx    For the pass products and the target.
         * @param bucket The bucket about to draw with it.
         */
        void bindBucketUniforms(Vkm::GL::Shader& shader, GLFrameContext& ctx, Bucket bucket) const;

        /**
         * @brief Call @p fn with each program the frame draws with.
         *
         * @param posed Whether skinned runs draw, so the skinned programs are in use.
         * @param fn    Called with each program in turn.
         */
        template<typename Fn>
        void forEachProgram(bool posed, Fn&& fn) {
            fn(m_shader);
            fn(m_plainShader);
            fn(m_layeredShader);
            if (!posed) return;
            fn(m_skinnedShader);
            fn(m_plainSkinnedShader);
        }

    private:
        Vkm::GL::Shader&  m_shader;              ///< Static geometry; borrowed, see the constructor.
        Vkm::GL::Shader&  m_layeredShader;       ///< Static, for a layered material; borrowed too.
        Vkm::GL::Shader   m_skinnedShader;       ///< Skinned, posed by the frame's palette.
        Vkm::GL::Shader   m_plainShader;         ///< Static, for a plain material.
        Vkm::GL::Shader   m_plainSkinnedShader;  ///< Skinned, for a plain material.
        GLInstanceBatcher m_batcher;

        // Back-to-front, refilled each frame with capacity kept. The opaque bucket is the context's.
        std::vector<std::pair<float, uint32_t>> m_transparent;
        std::vector<uint32_t>                   m_transparentSorted;
};

} // namespace Vkm::Engine
