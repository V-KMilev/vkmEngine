#pragma once

#include <memory>

#include "system/render/render_pass.h"
#include "resource/material_asset.h"
#include "resource/shader_asset.h"

namespace Core {
    class Texture2D;
}

namespace Engine {

/**
 * @brief Render pass for forward rendering using OpenGL.
 *
 * Supports multiple shader variants dispatched by MaterialType:
 * - Opaque/Transparent: PBR shader (with blending for transparent)
 * - Unlit: simplified shader without lighting
 */
class GLForwardPass : public RenderPass {
    public:
        GLForwardPass() = delete;
        ~GLForwardPass() override;

        GLForwardPass(const GLForwardPass& other) = delete;
        GLForwardPass& operator=(const GLForwardPass& other) = delete;

        GLForwardPass(GLForwardPass && other) = delete;
        GLForwardPass& operator=(GLForwardPass && other) = delete;

        /**
         * @brief Which material classes this pass draws.
         *
         * Split rendering: an Opaque pass (clears + draws opaque/unlit), then
         * the skybox fills the background, then a Transparent pass snapshots
         * that opaque+sky scene and draws transmissive/blended materials so
         * they refract what is actually behind them. @c All keeps the legacy
         * single-pass behaviour.
         */
        enum class Phase { All, Opaque, Transparent };

        /**
         * @brief Construct a GLForwardPass with a PBR shader and a phase.
         *
         * The phase is baked into the pass name so the editor's pipeline
         * dropdown can distinguish the opaque and transparent forward passes
         * - two instances live in the graph and addressing them by name only
         * works when the names differ.
         *
         * @param pbrShader Handle to the PBR shader asset. Sampler bindings live
         *                  on the asset and are reapplied automatically on hot reload.
         * @param phase Which material classes this pass draws (default All).
         */
        explicit GLForwardPass(ShaderHandle pbrShader, Phase phase = Phase::All);

    public:
        /**
         * @brief Set the shader for a specific material type. Pass an empty
         * handle to clear it.
         */
        void setShader(MaterialType type, ShaderHandle shader);

        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.read(RGResource::ShadowAtlas);
            builder.read(RGResource::IBL);
            builder.read(RGResource::AO);
            builder.write(RGResource::SceneHDR);
            // Transparent-phase passes can route to the OIT MRT when the
            // env toggle is on. Declare unconditionally for the transparent
            // phase so the graph's lifetime/validation tracking is right
            // whether OIT is enabled this frame or not.
            if (m_phase == Phase::Transparent || m_phase == Phase::All) {
                builder.write(RGResource::OITAccum);
                builder.write(RGResource::OITRevealage);
            }
        }

    private:
        ShaderHandle m_shaders[4] = {};  ///< Indexed by MaterialType (Opaque, Transparent, Unlit, AlphaMask)
        Phase        m_phase = Phase::All;

        /// Pre-integrated subsurface LUT, lazily built on first execute(). 64x16
        /// RGBA32F indexed by (NdotL, curvature); the PBR shader's HAS_SUBSURFACE
        /// branch samples it instead of the colored-wrap analytic form.
        std::unique_ptr<Core::Texture2D> m_sssLUT;
};

} // namespace Engine
