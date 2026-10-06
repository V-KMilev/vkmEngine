#include "gl_pass.h"

#include <GL/glew.h>

#include "gl_context.h"
#include "gl_error_handle.h"
#include "gl_frame_buffer.h"
#include "gl_shader.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "convention/gl_bindings.h"
#include "storage/gl_fog_volume.h"
#include "storage/gl_ibl.h"
#include "storage/gl_irradiance_volume.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

void GLPass::promoteColorChain(GLFrameContext& ctx) const {
    if (ctx.colorSrc != &ctx.sceneHDR) return;
    ctx.colorDst->blitColorFrom(*ctx.colorSrc);
    ctx.flipColor();
}

void GLPass::bindBackbufferViewport(GLFrameContext& ctx) const {
    const RenderView& view   = ctx.view;
    const int32_t     height = static_cast<int32_t>(view.viewportHeight);
    Vkm::GL::FrameBuffer::bindDefault();
    ctx.gl.setViewport(
        static_cast<int32_t>(view.viewportX),
        backbufferBottom(view, 0, height),
        static_cast<int32_t>(view.viewportWidth),
        height
    );
}

int32_t GLPass::backbufferBottom(const RenderView& view, int32_t top, int32_t height) {
    return static_cast<int32_t>(view.surfaceHeight) - static_cast<int32_t>(view.viewportY) - top - height;
}

void GLPass::bindFog(GLFrameContext& ctx, const Vkm::GL::Shader& shader) const {
    if (ctx.fogReady) {
        ctx.fog.bindIntegratedSlot(GLBindings::PostTextureSlots::FOG_VOLUME);
        shader.setUniform1f("u_fogDepth", ctx.fog.depth());
    }
    shader.setUniform1i("u_hasFog", ctx.fogReady ? 1 : 0);
}

void GLPass::bindAO(GLFrameContext& ctx, const Vkm::GL::Shader& shader, bool sampled) const {
    const bool hasAO = sampled && ctx.aoReady;
    if (hasAO) ctx.ao.bindTexture(GLTarget::Attachment::Color, GLBindings::PostTextureSlots::AO);
    shader.setUniform1i("u_hasAO", hasAO ? 1 : 0);
}

void GLPass::bindAmbient(GLFrameContext& ctx, const Vkm::GL::ShaderBase& shader) const {
    const RenderView& view = ctx.view;

    const bool hasIBL = ctx.ibl.isReady();
    if (hasIBL) ctx.ibl.bindIrradiance(GLBindings::IBLTextureSlots::IRRADIANCE);
    shader.setUniform1i("u_hasIBL", hasIBL ? 1 : 0);
    shader.setUniform1f("u_iblIntensity", view.environment.sky.intensity);

    // The baked SH volume, and the box that places a point in its grid.
    const bool hasIV = ctx.irradiance.isReady() && view.hasIrradianceVolume;
    shader.setUniform1i("u_hasIrradianceVolume", hasIV ? 1 : 0);
    if (!hasIV) return;
    ctx.irradiance.bindSlot(0, GLBindings::IrradianceVolumeSlots::SH0);
    ctx.irradiance.bindSlot(1, GLBindings::IrradianceVolumeSlots::SH1);
    ctx.irradiance.bindSlot(2, GLBindings::IrradianceVolumeSlots::SH2);
    ctx.irradiance.bindSlot(3, GLBindings::IrradianceVolumeSlots::SH3);
    const IrradianceVolumeData& iv = view.irradianceVolume;
    shader.setUniform3fv("u_ivMin",  iv.center - iv.halfExtents);
    shader.setUniform3fv("u_ivSize", iv.halfExtents * 2.0f);
    shader.setUniform1f("u_ivIntensity", iv.intensity);
    shader.setUniform1f("u_ivBlend",     iv.blendDistance);
}

void GLPass::setReflectRoughnessWritable(bool writable) const {
    // A draw-buffer index, which is the output location.
    constexpr GLuint WEIGHT = GLBindings::FragmentOutputs::REFLECT_WEIGHT;
    VKM_GL_CHECK(glColorMaski(WEIGHT, GL_TRUE, GL_TRUE, GL_TRUE, writable ? GL_TRUE : GL_FALSE));
}

} // namespace Vkm::Engine
