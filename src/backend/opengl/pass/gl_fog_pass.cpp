#include "pass/gl_fog_pass.h"

#include <algorithm>

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "gl_compute_shader.h"
#include "gl_error_handle.h"

#include "gl_frame_context.h"
#include "storage/gl_fog_volume.h"
#include "storage/gl_shadow_atlas.h"
#include "convention/gl_bindings.h"
#include "ecs/environment.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLFogPass::GLFogPass()
    : m_inject("shaders/fog/inject")
    , m_integrate("shaders/fog/integrate") {}

GLFogPass::~GLFogPass() = default;

void GLFogPass::execute(GLFrameContext& ctx) {
    const RenderView&  view = ctx.view;
    const Environment& env  = view.environment;
    if (!env.fog.enabled) return;

    // Reallocates only when the authored resolution changes.
    const glm::uvec3 dims = env.fog.froxelGrid();
    ctx.fog.resize(dims.x, dims.y, dims.z);

    // Slices reach the authored distance, not the far plane: over a kilometre most would hold air
    // too thin to show, and the near ones the eye reads would be coarse.
    const float depth = std::max(std::min(env.fog.maxDistance, view.camera.zFar), 2.0f * view.camera.zNear);
    ctx.fog.setDepth(depth);

    const glm::ivec3 idims(dims);
    namespace Groups = GLBindings::ComputeGroups;
    const uint32_t gx = Groups::covering(dims.x, Groups::IMAGE);
    const uint32_t gy = Groups::covering(dims.y, Groups::IMAGE);

    // Lights, cluster grid and ShadowBlock are bound already. The inject shader shadows the sun
    // and the spots from the 2D atlas and the point lights from their cubes.
    ctx.shadowAtlas.bind2D(GLBindings::ShadowTextureSlots::ATLAS_2D);
    ctx.shadowAtlas.bindCubes(GLBindings::ShadowTextureSlots::CUBE);

    ctx.fog.bindScatterImage(0, GL_WRITE_ONLY);
    m_inject.bind();
    bindAmbient(ctx, m_inject);
    m_inject.setUniform1f("u_density",        env.fog.density);
    m_inject.setUniform1f("u_height",         env.fog.height);
    m_inject.setUniform1f("u_heightFalloff",  env.fog.heightFalloff);
    m_inject.setUniform1f("u_anisotropy",     env.fog.anisotropy);
    m_inject.setUniform3fv("u_albedo",        env.fog.albedo);
    m_inject.setUniform3iv("u_froxelDims",    idims);
    m_inject.setUniform1f("u_fogDepth",       depth);
    m_inject.dispatch(gx, gy, dims.z);

    VKM_GL_CHECK(glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT));

    ctx.fog.bindScatterImage(0, GL_READ_ONLY);
    ctx.fog.bindIntegratedImage(1, GL_WRITE_ONLY);
    m_integrate.bind();
    m_integrate.setUniform3iv("u_froxelDims", idims);
    m_integrate.setUniform1f("u_fogDepth",    depth);
    m_integrate.dispatch(gx, gy, 1);

    // Order the integrated writes before the passes that fog sample the volume.
    VKM_GL_CHECK(glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT));

    ctx.fogReady = true;
}

} // namespace Vkm::Engine
