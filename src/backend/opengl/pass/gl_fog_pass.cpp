#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_fog_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "gl_compute_shader.h"

#include "gl_frame_context.h"
#include "data/gl_fog_volume.h"
#include "data/gl_shadow_atlas.h"
#include "convention/gl_bindings.h"
#include "ecs/environment.h"
#include "system/render/render_view.h"

namespace Engine {

namespace {
// Froxel grid is authored per scene (Environment); clamp each axis to a sane
// range so a bad value can't allocate a huge volume or a degenerate one.
constexpr uint32_t FROXEL_MIN = 16u;
constexpr uint32_t FROXEL_MAX = 512u;

uint32_t clampFroxel(uint32_t v) {
    return v < FROXEL_MIN ? FROXEL_MIN : (v > FROXEL_MAX ? FROXEL_MAX : v);
}
} // namespace

GLFogPass::GLFogPass()
    : m_inject(std::make_unique<Core::ComputeShader>("shaders/fog/inject"))
    , m_integrate(std::make_unique<Core::ComputeShader>("shaders/fog/integrate")) {}

GLFogPass::~GLFogPass() = default;

void GLFogPass::execute(GLFrameContext& ctx) {
    const RenderView&  view = ctx.view;
    const Environment& env  = view.environment;
    if (!env.fog.enabled) return;

    // Size the froxel volumes to the scene's authored fog resolution
    // (reallocates only when it changes; the first fog-enabled frame allocates).
    const glm::uvec3 dims(clampFroxel(env.fog.resolutionX),
                          clampFroxel(env.fog.resolutionY),
                          clampFroxel(env.fog.resolutionZ));
    ctx.fog.resize(dims.x, dims.y, dims.z);

    const float zNear = view.camera.zNear;
    const float zFar  = view.camera.zFar;
    const glm::mat4& invView = view.camera.invView;

    const glm::ivec3 idims(dims);
    const uint32_t gx = (dims.x + 7u) / 8u;
    const uint32_t gy = (dims.y + 7u) / 8u;

    // Injection: scatter each froxel's cluster lights into the scatter volume.
    // The light SSBO (binding 0) and cluster grid (binding 1) are already bound.
    // The 2D shadow atlas is not - the forward pass usually binds it and it runs
    // later - so bind it here for the sun's cascade lookup. The inject shader
    // never samples the point-light cubes, so those stay unbound. (The
    // ShadowBlock UBO is already bound by the backend.)
    ctx.shadowAtlas.bind2D(GLBindings::ShadowTextureSlots::Atlas2D);

    ctx.fog.bindScatterImage(0, GL_WRITE_ONLY);
    m_inject->bind();
    m_inject->setUniformMatrix4fv("u_invView",       invView);
    m_inject->setUniformMatrix4fv("u_invProjection", view.camera.invProjection);
    m_inject->setUniform3fv("u_cameraPos", view.camera.position);
    m_inject->setUniform1f("u_zNear", zNear);
    m_inject->setUniform1f("u_zFar",  zFar);
    m_inject->setUniform1f("u_density",       env.fog.density);
    m_inject->setUniform1f("u_height",        env.fog.height);
    m_inject->setUniform1f("u_heightFalloff", env.fog.heightFalloff);
    m_inject->setUniform1f("u_anisotropy",    env.fog.anisotropy);
    m_inject->setUniform3fv("u_albedo",       env.fog.albedo);
    m_inject->setUniform3iv("u_froxelDims",   idims);
    m_inject->dispatch(gx, gy, dims.z);

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // Integration: march each column front-to-back into the integrated volume.
    ctx.fog.bindScatterImage(0, GL_READ_ONLY);
    ctx.fog.bindIntegratedImage(1, GL_WRITE_ONLY);
    m_integrate->bind();
    m_integrate->setUniform1f("u_zNear", zNear);
    m_integrate->setUniform1f("u_zFar",  zFar);
    m_integrate->setUniform3iv("u_froxelDims", idims);
    m_integrate->dispatch(gx, gy, 1);

    // Order the integrated writes before the apply pass samples the volume.
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    ctx.fogReady = true;
}

} // namespace Engine
