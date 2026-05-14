#pragma once

#include <cstdint>

#include "system/render/render_pass.h"

namespace Core {
    class Shader;
}

namespace Engine {

/**
 * @brief Renders depth maps for every shadow-casting light in the RenderView.
 *
 * Each frame:
 *  1. Iterates RenderView::lights; every light with shadowSlot >= 0 is a
 *     caster. Slot assignment (and the per-light shadow parameters) is done
 *     by RenderView::build — this pass just consumes them.
 *  2. For directional / spot, writes one 2D-array layer at shadowSlot.
 *     For point, writes six cube faces at shadowSlot.
 *  3. Writes the matching Shadow{2D,Cube}CasterGPU entry into the shadow UBO.
 *  4. Draws shadow-castable opaque batches once per layer / per face. The
 *     batcher reuses the global instance buffer; the prefix
 *     `shadowInstanceCount` of each batch covers only castShadows=true
 *     drawables (sorted to the front by RenderView).
 *
 * A single depth shader covers 2D and cube paths — both write standard
 * projected depth (no gl_FragDepth override).
 */
class GLShadowPass : public RenderPass {
    public:
        GLShadowPass() = delete;
        ~GLShadowPass() override = default;

        GLShadowPass(const GLShadowPass&) = delete;
        GLShadowPass& operator=(const GLShadowPass&) = delete;
        GLShadowPass(GLShadowPass&&) = delete;
        GLShadowPass& operator=(GLShadowPass&&) = delete;

        explicit GLShadowPass(Core::Shader& depthShader);

    public:
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderBackend& backend, const RenderView& view, const ResourceManager& resources) override;

    private:
        Core::Shader& m_depthShader;
};

} // namespace Engine
