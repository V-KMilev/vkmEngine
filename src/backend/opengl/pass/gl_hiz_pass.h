#pragma once

#include <memory>

#include "resource/shader_asset.h"
#include "system/render/render_pass.h"

namespace Core {
    class ScreenTriangle;
}

namespace Engine {

/**
 * @brief Build the Hi-Z (max-Z) depth pyramid for the frame.
 *
 * Mip 0 is initialised from the prepass view-space position target
 * (init shader writes -pos.z); each subsequent mip is the max(2x2)
 * reduction of the level below (reduce shader). The pyramid lives in
 * GLHiZ via RGResource::HiZPyramid; the visibility system reads back
 * one mip via OcclusionOracle to AABB-test occluders one frame late.
 * Gated by env.occlusion.useHiZ; off by default.
 */
class GLHiZPass : public RenderPass {
    public:
        GLHiZPass() = delete;
        ~GLHiZPass() override;

        GLHiZPass(const GLHiZPass& other) = delete;
        GLHiZPass& operator=(const GLHiZPass& other) = delete;

        GLHiZPass(GLHiZPass && other) = delete;
        GLHiZPass& operator=(GLHiZPass && other) = delete;

        GLHiZPass(ShaderHandle initShader, ShaderHandle reduceShader);

    public:
        bool enabledForView(const RenderView& view) const override;

        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.read(RGResource::GBufferPosition);
            builder.write(RGResource::HiZPyramid);
        }

    private:
        ShaderHandle m_initShader;
        ShaderHandle m_reduceShader;
        std::unique_ptr<Core::ScreenTriangle> m_screenTri;
};

} // namespace Engine
