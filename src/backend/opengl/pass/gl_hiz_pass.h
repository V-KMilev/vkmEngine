#pragma once

#include <memory>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "resource/shader_asset.h"
#include "gl_render_pass.h"

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
class GLHiZPass : public GLRenderPass {
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
        void executeGL(GLBackend& gl, RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.read(RGResource::GBufferPosition);
            builder.write(RGResource::HiZPyramid);
        }

    private:
        ShaderHandle m_initShader;
        ShaderHandle m_reduceShader;
        std::unique_ptr<Core::ScreenTriangle> m_screenTri;

        // Double-buffered PBO ring for asynchronous Hi-Z readback: glReadPixels
        // into one buffer returns immediately (no CPU<->GPU sync); the other,
        // filled last frame, is mapped to publish. Each buffer carries the
        // view/viewProj its depth was rendered with so the oracle stays in sync
        // despite the extra frame of latency.
        static constexpr int PBO_RING = 2;
        GLuint    m_pbo[PBO_RING]      = {};
        bool      m_pboValid[PBO_RING] = {};
        glm::mat4 m_pboView[PBO_RING]{};
        glm::mat4 m_pboViewProj[PBO_RING]{};
        int       m_pboIndex = 0;
        int       m_pboW     = 0;
        int       m_pboH     = 0;
};

} // namespace Engine
