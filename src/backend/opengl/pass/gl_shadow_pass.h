#pragma once

#include <cstdint>

#include "resource/shader_asset.h"
#include "gl_render_pass.h"

namespace Engine {

/**
 * @brief Renders depth maps for every shadow-casting light in the RenderView.
 *
 * Each frame:
 *  1. Iterates RenderView::lights; every light with shadowSlot >= 0 is a
 *     caster. Slot assignment (and the per-light shadow parameters) is done
 *     by RenderView::build - this pass just consumes them.
 *  2. For directional / spot, writes one 2D-array layer at shadowSlot.
 *     For point, writes six cube faces at shadowSlot.
 *  3. Writes the matching Shadow{2D,Cube}CasterGPU entry into the shadow UBO.
 *  4. Draws shadow-castable opaque batches once per layer / per face. The
 *     batcher reuses the global instance buffer; the prefix
 *     `shadowInstanceCount` of each batch covers only castShadows=true
 *     drawables (sorted to the front by RenderView).
 *
 * A single depth shader covers 2D and cube paths - both write standard
 * projected depth (no gl_FragDepth override).
 */
class GLShadowPass : public GLRenderPass {
    public:
        GLShadowPass() = delete;
        ~GLShadowPass() override = default;

        GLShadowPass(const GLShadowPass& other) = delete;
        GLShadowPass& operator=(const GLShadowPass& other) = delete;

        GLShadowPass(GLShadowPass && other) = delete;
        GLShadowPass& operator=(GLShadowPass && other) = delete;

        explicit GLShadowPass(ShaderHandle depthShader);

    public:
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void executeGL(GLBackend& gl, RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.write(RGResource::ShadowAtlas);
        }

    private:
        ShaderHandle m_depthShader;

        // Dirty-skip: a static scene + static sun re-rasterizing 4 cascades +
        // 6 cube faces every frame is pure waste. When the shadow signature
        // (camera frustum, caster lights, caster geometry) is unchanged we
        // skip the whole pass - the atlas depth and the Shadow UBO (binding 3,
        // nothing else rebinds it) stay valid from the last render.
        uint64_t m_lastSig  = 0;
        bool     m_havePrev = false;
};

} // namespace Engine
