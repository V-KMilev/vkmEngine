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

        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.read(RGResource::ShadowAtlas);
            builder.read(RGResource::IBL);
            builder.read(RGResource::AO);
            builder.write(RGResource::SceneHDR);
        }

    private:
        ShaderHandle m_shaders[3] = {};  ///< Indexed by MaterialType (Opaque, Transparent, Unlit)
};

} // namespace Engine
