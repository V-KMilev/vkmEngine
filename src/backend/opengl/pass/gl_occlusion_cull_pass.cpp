#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_occlusion_cull_pass.h"

#include <GL/glew.h>

#include "gl_compute_shader.h"

#include "convention/gl_bindings.h"
#include "data/gl_hiz.h"
#include "data/gl_instance_batcher.h"
#include "gl_frame_context.h"
#include "system/render/render_view.h"

namespace Engine {

namespace {
constexpr uint32_t GROUP_SIZE = 64;   // matches local_size_x in the shader
} // namespace

GLOcclusionCullPass::GLOcclusionCullPass()
    : m_compute(std::make_unique<Core::ComputeShader>("shaders/occlusion_cull")) {}

GLOcclusionCullPass::~GLOcclusionCullPass() = default;

void GLOcclusionCullPass::execute(GLFrameContext& ctx) {
    // Not running leaves the batch un-culled, and an un-culled batch draws
    // straight from the CPU counts - so switching this off costs nothing rather
    // than compacting instances no one asked to cull. Same on the first frame
    // after a resize, when the pyramid describes a viewport that no longer
    // exists.
    const GLHiZ& hiz = ctx.hiz;
    if (!ctx.view.settings.occlusionCulling || !hiz.isBuilt()) return;

    const uint32_t instances = ctx.opaqueBatch.instanceCount();
    if (instances == 0) return;
    if (!ctx.opaqueBatch.bindCullBuffers()) return;

    hiz.bind(GLBindings::PostTextureSlots::HiZ);

    m_compute->bind();
    m_compute->setUniformMatrix4fv("u_viewProjection", ctx.view.camera.viewProjection);
    m_compute->setUniform1ui("u_instanceCount", instances);
    m_compute->setUniform1i("u_hiz", static_cast<int>(GLBindings::PostTextureSlots::HiZ));
    m_compute->setUniform2f("u_hizSize", static_cast<float>(hiz.width(0)),
                                         static_cast<float>(hiz.height(0)));
    m_compute->setUniform1f("u_hizMaxLod", static_cast<float>(hiz.mipCount() - 1));

    m_compute->dispatch((instances + GROUP_SIZE - 1) / GROUP_SIZE);

    // The compacted matrices are read as vertex attributes and the counts as
    // draw commands, so both of those uses need the writes visible - neither is
    // an SSBO read, and neither barrier bit implies the other.
    glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
}

} // namespace Engine
