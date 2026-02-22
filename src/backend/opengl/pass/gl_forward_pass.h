#pragma once

#include "render/render_pass.h"
#include "resource/material_asset.h"

namespace Core {
    class Shader;
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
        ~GLForwardPass() = default;

        GLForwardPass(const GLForwardPass& other) = delete;
        GLForwardPass& operator=(const GLForwardPass& other) = delete;

        GLForwardPass(GLForwardPass && other) = delete;
        GLForwardPass& operator=(GLForwardPass && other) = delete;

        /**
         * @brief Construct a GLForwardPass with a PBR shader (used for Opaque and Transparent).
         * @param pbrShader Reference to the PBR shader.
         */
        GLForwardPass(Core::Shader& pbrShader);

    public:
        /**
         * @brief Set the shader for a specific material type.
         * @param type The material type.
         * @param shader Reference to the shader to use.
         */
        void setShader(MaterialType type, Core::Shader& shader);

        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderBackend& backend, const RenderView& view, const ResourceManager& resources) override;

    private:
        void setupSamplers(Core::Shader& shader);

        Core::Shader* m_shaders[3] = {};  ///< Indexed by MaterialType (Opaque, Transparent, Unlit)
};

} // namespace Engine
