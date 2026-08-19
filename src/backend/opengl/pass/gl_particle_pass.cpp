#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "pass/gl_particle_pass.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_shader.h"
#include "gl_context.h"
#include "gl_shader_storage_buffer.h"

#include "gl_frame_context.h"
#include "gl_target.h"
#include "convention/gl_bindings.h"
#include "system/render/data/particle_data.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

GLParticlePass::GLParticlePass()
    : m_shader(std::make_unique<Vkm::GL::Shader>("shaders/particle")) {}

GLParticlePass::~GLParticlePass() = default;

void GLParticlePass::drawBatch(const std::vector<ParticleData>& batch) {
    if (batch.empty()) return;

    const uint32_t count = static_cast<uint32_t>(batch.size());
    const uint32_t bytes = count * static_cast<uint32_t>(sizeof(ParticleData));

    // Grow the instance buffer on demand; steady state re-uploads in place.
    if (!m_instances || count > m_capacity) {
        m_instances = std::make_unique<Vkm::GL::ShaderStorageBuffer>(batch.data(), bytes, GL_DYNAMIC_DRAW);
        m_capacity  = count;
    } else {
        m_instances->update(batch.data(), bytes);
    }
    m_instances->bindBase(GLBindings::SSBOBindingPoints::Particles);

    // Attribute-less: per-particle data is read from the SSBO by gl_InstanceID.
    m_vao.bind();
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(count));
}

void GLParticlePass::execute(GLFrameContext& ctx) {
    const RenderView& view = ctx.view;
    if (view.particlesAdditive.empty() && view.particlesAlpha.empty()) return;

    // Depth-tested against the geometry already drawn but never writing depth -
    // particles occlude nothing.
    ctx.sceneRender.bind(ctx.gl);
    ctx.gl.setDepthTest(true);
    ctx.gl.setDepthFunc(GL_LEQUAL);
    ctx.gl.setDepthWrite(false);
    ctx.gl.setFaceCulling(false);
    ctx.gl.setBlending(true);

    m_shader->bind();
    m_shader->setUniformMatrix4fv("u_viewProj", view.camera.viewProjection);

    // Billboard axes: the view matrix's rows are the camera's right / up in world
    // space, so every quad faces the camera.
    const glm::mat4& V = view.camera.view;
    m_shader->setUniform3fv("u_camRight", glm::vec3(V[0][0], V[1][0], V[2][0]));
    m_shader->setUniform3fv("u_camUp",    glm::vec3(V[0][1], V[1][1], V[2][1]));

    // Additive first (order-independent), then the sorted alpha bucket.
    if (!view.particlesAdditive.empty()) {
        ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE);
        drawBatch(view.particlesAdditive);
    }
    if (!view.particlesAlpha.empty()) {
        ctx.gl.setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        drawBatch(view.particlesAlpha);
    }

    ctx.gl.setBlending(false);
    ctx.gl.setDepthWrite(true);
}

} // namespace Vkm::Engine
