#include "pass/gl_pass.h"

#include "vkm_opengl/core/context.h"
#include "vkm_opengl/shader/shader.h"
#include "vkm_opengl/target/frame_buffer.h"

#include "pass/gl_frame_context.h"
#include "storage/gl_target.h"
#include "convention/gl_bindings.h"
#include "storage/light/gl_atmosphere.h"
#include "storage/light/gl_fog_volume.h"
#include "storage/light/gl_ibl.h"
#include "storage/light/gl_irradiance_volume.h"
#include "ecs/settings/environment.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

void GLPass::bindBackbufferViewport(GLFrameContext& ctx) const {
    const RenderView& view   = ctx.view;
    const int32_t     height = static_cast<int32_t>(view.viewportHeight);
    Vkm::OpenGL::FrameBuffer::bindDefault();
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

void GLPass::bindFog(GLFrameContext& ctx, const Vkm::OpenGL::Shader& shader, bool air) const {
    if (ctx.fogReady) {
        ctx.fog.bindIntegratedSlot(GLBindings::PostTextureSlots::FOG_VOLUME);
        shader.setUniform1f("u_fogDepth", ctx.fog.depth());
    }
    shader.setUniform1i("u_hasFog", ctx.fogReady ? 1 : 0);
    if (!air) return;

    if (ctx.aerialPerspectiveReady) {
        ctx.atmosphere.bindAerialPerspective(GLBindings::PostTextureSlots::AERIAL_PERSPECTIVE);
        shader.setUniform1f("u_airDepth", ctx.atmosphere.aerialDepth());
    }
    shader.setUniform1i("u_hasAir", ctx.aerialPerspectiveReady ? 1 : 0);
}

void GLPass::bindAO(GLFrameContext& ctx, const Vkm::OpenGL::Shader& shader, bool sampled) const {
    const bool hasAO = sampled && ctx.aoReady;
    if (hasAO) ctx.ao.bindTexture(GLTarget::Attachment::Color, GLBindings::PostTextureSlots::AO);
    shader.setUniform1i("u_hasAO", hasAO ? 1 : 0);
}

void GLPass::bindAmbient(GLFrameContext& ctx, const Vkm::OpenGL::ShaderBase& shader, bool probes) const {
    const RenderView& view = ctx.view;

    const bool hasIBL = ctx.ibl.isReady();
    if (hasIBL) ctx.ibl.bindIrradiance(GLBindings::IBLTextureSlots::IRRADIANCE);
    shader.setUniform1i("u_hasIBL", hasIBL ? 1 : 0);
    shader.setUniform1f("u_iblIntensity", view.environment->sky.intensity);
    shader.setUniform1i("u_probeCount", probes ? ctx.probeCount : 0);

    // The baked SH volume, and the box that places a point in its grid.
    if (ctx.irradiance.isReady() && view.lists.hasIrradianceVolume) {
        ctx.irradiance.bindForShading(shader, ctx.irradiance.placed(view.lists.irradianceVolume));
    } else {
        shader.setUniform1i("u_hasIrradianceVolume", 0);
    }
}

void GLPass::setReflectRoughnessWritable(Vkm::OpenGL::Context& gl, bool writable) const {
    // A draw-buffer index, which is the output location.
    constexpr uint32_t WEIGHT = GLBindings::FragmentOutputs::REFLECT_WEIGHT;
    gl.setColorMask(WEIGHT, true, true, true, writable);
}

} // namespace Vkm::Engine
