#include "gl_shadow_pass.h"

#include <cmath>

#include <GL/glew.h>

#include "logger.h"
#include "debug/statistics.h"

#include "core/gl_backend.h"
#include "config/gl_config.h"
#include "gl_shader.h"

#include "resource/gl_instance_buffer.h"
#include "resource/gl_mesh.h"
#include "resource/gl_shadow_data.h"
#include "resource/gl_shadow_map.h"

#include "core/math/axes.h"
#include "core/math/projection.h"
#include "core/math/rotation.h"
#include "ecs/component/transform.h"
#include "resource/material_asset.h"
#include "resource/resource_manager.h"
#include "system/render/render_view.h"

namespace Engine {

namespace {

// SHADOW_CUBE_NEAR in shaders/pbr/fragmentShader.shader mirrors this value.
constexpr float kCubeNear = 0.1f;

glm::mat4 directionalLightSpace(
    const glm::quat& rotation,
    const glm::vec3& focus,
    float extent,
    uint32_t resolution
) {
    // Heuristic far/distance derived from the user-facing extent so a single
    // knob on the Light component drives the whole ortho frustum.
    const float distance = extent * 1.5f;
    const float near     = 0.5f;
    const float far      = extent * 3.0f;

    const glm::vec3 forward = Math::computeForward(rotation);
    const glm::vec3 up      = Math::computeUp(rotation);
    const glm::vec3 right   = Math::computeRight(rotation);

    // Snap focus to shadow-texel grid so the ortho frustum only translates
    // in whole-texel increments as the camera moves. Without this the
    // projected shadow texels slide under fragments and edges shimmer.
    const float texelSize = (2.0f * extent) / static_cast<float>(resolution);
    const float r = std::floor(glm::dot(focus, right) / texelSize) * texelSize;
    const float u = std::floor(glm::dot(focus, up)    / texelSize) * texelSize;
    const float f = glm::dot(focus, forward);
    const glm::vec3 snapped = right * r + up * u + forward * f;

    const glm::mat4 proj = Math::makeOrthographic(extent, 1.0f, near, far);
    return proj * Transform::computeView({snapped - forward * distance, rotation});
}

glm::mat4 spotLightSpace(
    const glm::vec3& position, const glm::quat& rotation,
    float outerConeAngle, float range
) {
    // Multiply outerCone by 2 to fit the full cone in the camera's FOV;
    // clamp so we never degenerate or exceed a reasonable shadow frustum.
    const float fov  = glm::clamp(outerConeAngle * 2.0f,
                                  glm::radians(5.0f), glm::radians(170.0f));
    // Near scales with range so small spot lights don't waste depth precision.
    const float near = std::max(range * 0.01f, 0.01f);
    const glm::mat4 proj = Math::makePerspective(fov, 1.0f, near, std::max(range, 1.0f));
    return proj * Transform::computeView({position, rotation});
}

void buildCubeFaceMatrices(const glm::vec3& pos, float range, glm::mat4 out[6]) {
    const glm::mat4 proj = Math::makePerspective(
        glm::radians(90.0f), 1.0f, kCubeNear, std::max(range, 1.0f));

    // Standard OpenGL cube-map face conventions: face direction + the up
    // vector that orients the face's u/v axes consistently across the cube.
    out[0] = proj * glm::lookAt(pos, pos +  Math::WORLD_AXIS_X_RIGHT,   -Math::WORLD_AXIS_Y_UP);      // +X
    out[1] = proj * glm::lookAt(pos, pos + -Math::WORLD_AXIS_X_RIGHT,   -Math::WORLD_AXIS_Y_UP);      // -X
    out[2] = proj * glm::lookAt(pos, pos +  Math::WORLD_AXIS_Y_UP,       Math::WORLD_AXIS_Z_FORWARD); // +Y
    out[3] = proj * glm::lookAt(pos, pos + -Math::WORLD_AXIS_Y_UP,      -Math::WORLD_AXIS_Z_FORWARD); // -Y
    out[4] = proj * glm::lookAt(pos, pos +  Math::WORLD_AXIS_Z_FORWARD, -Math::WORLD_AXIS_Y_UP);      // +Z
    out[5] = proj * glm::lookAt(pos, pos + -Math::WORLD_AXIS_Z_FORWARD, -Math::WORLD_AXIS_Y_UP);      // -Z
}

} // namespace

GLShadowPass::GLShadowPass(Core::Shader& depthShader)
    : RenderPass("GLShadowPass")
    , m_depthShader(depthShader)
{
}

void GLShadowPass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // Shadow map resolution is fixed and independent of the window size.
}

void GLShadowPass::execute(RenderBackend& backend, const RenderView& view, const ResourceManager& /*resources*/) {
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLShadowPass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }

    auto& gl         = static_cast<GLBackend&>(backend);
    auto& glView     = gl.getView();
    auto& atlas      = glView.getShadowAtlas();
    auto& shadowData = glView.getShadowData();

    shadowData.clear();

    auto& batcher        = glView.getInstanceBatcher();
    const auto& batches  = batcher.getBatches();
    auto& instanceBuffer = batcher.getBuffer();

    auto& ctx = gl.getContext();
    const bool   prevCullEnabled = ctx.isFaceCullingEnabled();
    const GLenum prevCullFace    = ctx.getCullFace();

    ctx.setDepthTest(true);
    ctx.setDepthWrite(true);
    // Front-face culling pushes captured depth to the back of each closed
    // mesh, hiding self-shadow acne for 2D and cube targets alike.
    ctx.setFaceCulling(true);
    ctx.setCullFace(GL_FRONT);

    m_depthShader.bind();
    STATS_RECORD_SHADER_SWITCH();

    auto drawShadowBatches = [&]() {
        for (const auto& batch : batches) {
            if (batch.materialType != MaterialType::Opaque) continue;
            if (batch.shadowInstanceCount == 0) continue;

            GLMesh* mesh = glView.getMutableMesh(batch.mesh);
            if (!mesh) continue;

            instanceBuffer.attachToVAO(*mesh->getVAO(), GLConfig::InstanceAttributes::ModelMatrixStart);
            mesh->drawInstancedBaseInstance(GL_TRIANGLES, batch.shadowInstanceCount, batch.firstInstance);
        }
    };

    // Walk lights once: build the matrix, write the UBO entry, render the
    // corresponding atlas slot — no second matrix recomputation.
    uint32_t count2D = 0, countCube = 0;
    glm::mat4 cubeFaces[6];

    for (const auto& light : view.lights) {
        if (light.shadowSlot < 0) continue;
        const uint32_t slot = static_cast<uint32_t>(light.shadowSlot);

        if (light.type == LightType::Point) {
            const float range = std::max(light.radius, 1.0f);
            buildCubeFaceMatrices(light.position, range, cubeFaces);

            ShadowCubeCasterGPU entry;
            entry.params = glm::vec4(light.shadowBias, range, 0.0f, 0.0f);
            shadowData.setCasterCube(slot, entry);
            ++countCube;

            for (uint32_t face = 0; face < 6; ++face) {
                atlas.bindCubeFaceForWriting(slot, face);
                m_depthShader.setUniformMatrix4fv(GLConfig::UniformNames::LightSpace, cubeFaces[face]);
                drawShadowBatches();
            }
        } else {
            const glm::mat4 lightSpace = (light.type == LightType::Directional)
                ? directionalLightSpace(light.rotation, view.camera.position,
                                        light.shadowExtent, GLConfig::Limits::ShadowResolution2D)
                : spotLightSpace(light.position, light.rotation,
                                 light.outerConeAngle, light.radius);

            Shadow2DCasterGPU entry;
            entry.lightSpace = lightSpace;
            entry.params     = glm::vec4(light.shadowBias, 0.0f, 0.0f, 0.0f);
            shadowData.setCaster2D(slot, entry);
            ++count2D;

            atlas.bind2DLayerForWriting(slot);
            m_depthShader.setUniformMatrix4fv(GLConfig::UniformNames::LightSpace, lightSpace);
            drawShadowBatches();
        }
    }

    shadowData.setCounts(count2D, countCube);
    shadowData.uploadAndBind();

    atlas.unbindForWriting();

    // Restore: cull state via ctx (cached, dedup'd), viewport back to the
    // full window (matches GLBackend::resize's canonical setting).
    ctx.setCullFace(prevCullFace);
    ctx.setFaceCulling(prevCullEnabled);
    ctx.setViewport(0, 0,
        static_cast<int32_t>(view.viewportWidth),
        static_cast<int32_t>(view.viewportHeight));
}

} // namespace Engine
