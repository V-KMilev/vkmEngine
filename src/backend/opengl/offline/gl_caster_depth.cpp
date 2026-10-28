#include "offline/gl_caster_depth.h"

#include "gl_context.h"

#include "gl_view.h"
#include "asset/gl_material.h"
#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "frame/gl_object_buffer.h"

namespace Vkm::Engine {

GLCasterDepth::GLCasterDepth()
    : m_depth("shaders/shadow/depth")
    , m_masked("shaders/shadow/depth_masked") {}

GLCasterDepth::~GLCasterDepth() = default;

void GLCasterDepth::draw(
    Vkm::GL::Context& gl,
    const std::vector<uint32_t>& casters,
    const RenderObjects& scene,
    const GLView& glView,
    const GLObjectBuffer& objects,
    const glm::mat4& lightVP,
    uint32_t side,
    MeshOf meshes
) {
    gl.setViewport(0, 0, static_cast<int32_t>(side), static_cast<int32_t>(side));
    gl.setDepthTest(true);
    gl.setDepthWrite(true);
    gl.setDepthFunc(GLDepth::SHADOW_TEST);
    gl.setBlending(false);
    gl.setFaceCulling(false);
    gl.clear(false, true, false, GLDepth::SHADOW_CLEAR);
    if (casters.empty()) return;

    objects.bind();
    m_masked.bind();
    m_masked.setUniformMatrix4fv("u_lightVP", lightVP);
    m_masked.setUniform2f("u_ditherShift", 0.0f, 0.0f);
    m_depth.bind();
    m_depth.setUniformMatrix4fv("u_lightVP", lightVP);
    const Vkm::GL::Shader* bound = &m_depth;
    for (const InstanceDraw& draw : m_batcher.buildGrouped(casters, scene, glView, 0, meshes)) {
        const GLMaterial* material = glView.getMaterial(draw.material);
        const bool        masked   = material && material->cutsItsShadow();
        Vkm::GL::Shader&  program  = masked ? m_masked : m_depth;
        if (&program != bound) {
            program.bind();
            bound = &program;
        }
        if (masked) {
            material->bind(GLBindings::UBOBindingPoints::MATERIAL);
            material->bindTextures(glView);
        }
        m_batcher.draw(gl, draw);
    }
}

} // namespace Vkm::Engine
