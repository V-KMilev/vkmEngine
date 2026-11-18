#include "offline/capture/gl_caster_depth.h"

#include "vkm_opengl/core/context.h"

#include "asset/gl_asset_mirror.h"
#include "asset/gl_material.h"
#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "frame/gl_object_buffer.h"

namespace Vkm::Engine {

GLCasterDepth::GLCasterDepth()
    : m_depth("shaders/opengl/scene/shadow")
    , m_masked("shaders/opengl/scene/shadow/masked") {}

GLCasterDepth::~GLCasterDepth() = default;

void GLCasterDepth::draw(
    Vkm::OpenGL::Context& gl,
    const std::vector<uint32_t>& casters,
    const RenderObjects& scene,
    const GLAssetMirror& mirror,
    const GLObjectBuffer& objects,
    const glm::mat4& lightVP,
    uint32_t side,
    MeshOf meshes,
    bool clear
) {
    gl.setViewport(0, 0, static_cast<int32_t>(side), static_cast<int32_t>(side));
    gl.setDepthTest(true);
    gl.setDepthWrite(true);
    gl.setDepthFunc(GLDepth::SHADOW_TEST);
    gl.setBlending(false);
    gl.setFaceCulling(false);
    if (clear) gl.clear(false, true, GLDepth::SHADOW_CLEAR);
    if (casters.empty()) return;

    objects.bind();
    m_masked.bind();
    m_masked.setUniformMatrix4fv("u_lightVP", lightVP);
    m_masked.setUniform2f("u_ditherShift", 0.0f, 0.0f);
    m_depth.bind();
    m_depth.setUniformMatrix4fv("u_lightVP", lightVP);
    const Vkm::OpenGL::Shader* bound = &m_depth;
    for (const InstanceDraw& draw : m_batcher.buildGrouped(casters, scene, mirror, 0, meshes)) {
        const GLMaterial* material = mirror.getMaterial(draw.material);
        const bool masked = material && material->cutsItsShadow();
        Vkm::OpenGL::Shader& program = masked ? m_masked : m_depth;
        if (&program != bound) {
            program.bind();
            bound = &program;
        }
        if (masked) {
            material->bind(GLBindings::UBOBindingPoints::MATERIAL);
            material->bindTextures(mirror);
        }
        m_batcher.draw(gl, draw);
    }
}

} // namespace Vkm::Engine
