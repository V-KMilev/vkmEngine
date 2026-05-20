#pragma once

#include "system/render/render_pass.h"
#include "resource/material_asset.h"
#include "resource/shader_asset.h"

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
        ~GLForwardPass() = default;

        GLForwardPass(const GLForwardPass& other) = delete;
        GLForwardPass& operator=(const GLForwardPass& other) = delete;

        GLForwardPass(GLForwardPass && other) = delete;
        GLForwardPass& operator=(GLForwardPass && other) = delete;

        /**
         * @brief Construct a GLForwardPass with a PBR shader (used for Opaque and Transparent).
         * @param pbrShader Handle to the PBR shader asset. Sampler bindings live
         *                  on the asset and are reapplied automatically on hot reload.
         */
        explicit GLForwardPass(ShaderHandle pbrShader);

    public:
        /**
         * @brief Set the shader for a specific material type. Pass an empty
         * handle to clear it.
         */
        void setShader(MaterialType type, ShaderHandle shader);

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
        void setPhase(Phase p) { m_phase = p; }

        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.read(RGResource::ShadowAtlas);
            builder.read(RGResource::IBL);
            builder.read(RGResource::AO);
            builder.write(RGResource::SceneHDR);
        }

    private:
        ShaderHandle m_shaders[4] = {};  ///< Indexed by MaterialType (Opaque, Transparent, Unlit, AlphaMask)
        Phase        m_phase = Phase::All;
};

} // namespace Engine
