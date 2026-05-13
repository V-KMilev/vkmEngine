#include "gl_shadow_pass.h"

#include <cmath>

#include <GL/glew.h>

#include <glm/gtc/matrix_transform.hpp>

#include "logger.h"
#include "debug/statistics.h"

#include "core/gl_backend.h"
#include "config/gl_config.h"
#include "gl_shader.h"

#include "resource/gl_instance_buffer.h"
#include "resource/gl_mesh.h"
#include "resource/gl_shadow_data.h"
#include "resource/gl_shadow_map.h"

#include "resource/material_asset.h"
#include "resource/resource_manager.h"
#include "system/render/render_view.h"

namespace Engine {

namespace {

glm::vec3 forwardFromRotation(const glm::quat& rotation) {
    return glm::normalize(rotation * glm::vec3(0.0f, 0.0f, 1.0f));
}

glm::mat4 directionalLightSpace(
    const glm::vec3& lightDir,
    const glm::vec3& focus,
    float orthoRadius, float distance, float near, float far
) {
    glm::vec3 up = (std::abs(lightDir.y) > 0.99f)
        ? glm::vec3(0.0f, 0.0f, 1.0f)
        : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::mat4 view = glm::lookAt(focus - lightDir * distance, focus, up);
    const glm::mat4 proj = glm::ortho(-orthoRadius, orthoRadius,
                                      -orthoRadius, orthoRadius, near, far);
    return proj * view;
}

glm::mat4 spotLightSpace(
    const glm::vec3& position, const glm::vec3& direction, float outerConeAngle, float range
) {
    glm::vec3 up = (std::abs(direction.y) > 0.99f)
        ? glm::vec3(0.0f, 0.0f, 1.0f)
        : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::mat4 view = glm::lookAt(position, position + direction, up);
    // Multiply outerCone by 2 to fit the full cone in the camera's FOV;
    // clamp so we never degenerate or exceed a reasonable shadow frustum.
    const float fov = glm::clamp(outerConeAngle * 2.0f, glm::radians(5.0f), glm::radians(170.0f));
    const glm::mat4 proj = glm::perspective(fov, 1.0f, 0.1f, std::max(range, 1.0f));
    return proj * view;
}

void buildCubeFaceMatrices(const glm::vec3& pos, float range, glm::mat4 out[6]) {
    const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, std::max(range, 1.0f));

    // Standard OpenGL cube-map face conventions.
    out[0] = proj * glm::lookAt(pos, pos + glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)); // +X
    out[1] = proj * glm::lookAt(pos, pos + glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)); // -X
    out[2] = proj * glm::lookAt(pos, pos + glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)); // +Y
    out[3] = proj * glm::lookAt(pos, pos + glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)); // -Y
    out[4] = proj * glm::lookAt(pos, pos + glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)); // +Z
    out[5] = proj * glm::lookAt(pos, pos + glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f)); // -Z
}

} // namespace

GLShadowPass::GLShadowPass(Core::Shader& shader2D, Core::Shader& shaderCube)
    : RenderPass("GLShadowPass")
    , m_shader2D(shader2D)
    , m_shaderCube(shaderCube)
{
}

void GLShadowPass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // Shadow map resolution is fixed and independent of the window size.
}

void GLShadowPass::collectCasters(
    const RenderView& view,
    std::vector<Job2D>& jobs2D,
    std::vector<PointJob>& jobsCube
) {
    jobs2D.clear();
    jobsCube.clear();

    const uint32_t max2D   = GLConfig::Limits::MaxShadowCasters2D;
    const uint32_t maxCube = GLConfig::Limits::MaxShadowCastersCube;
    const uint32_t maxTotal = GLConfig::Limits::MaxShadowCasters;

    for (size_t i = 0; i < view.lights.size(); ++i) {
        const auto& light = view.lights[i];
        if (!light.castShadows) continue;

        const uint32_t taken = static_cast<uint32_t>(jobs2D.size() + jobsCube.size());
        if (taken >= maxTotal) break;

        if (light.type == LightType::Directional && jobs2D.size() < max2D) {
            Job2D job;
            job.lightIndex = static_cast<int>(i);
            job.layer      = static_cast<uint32_t>(jobs2D.size());
            job.lightSpace = directionalLightSpace(
                forwardFromRotation(light.rotation),
                view.camera.position,
                m_dirOrthoRadius, m_dirDistance, m_dirNear, m_dirFar
            );
            jobs2D.push_back(job);
        }
        else if (light.type == LightType::Spot && jobs2D.size() < max2D) {
            Job2D job;
            job.lightIndex = static_cast<int>(i);
            job.layer      = static_cast<uint32_t>(jobs2D.size());
            job.lightSpace = spotLightSpace(
                light.position,
                forwardFromRotation(light.rotation),
                light.outerConeAngle,
                light.radius
            );
            jobs2D.push_back(job);
        }
        else if (light.type == LightType::Point && jobsCube.size() < maxCube) {
            PointJob job;
            job.lightIndex = static_cast<int>(i);
            job.cubeIndex  = static_cast<uint32_t>(jobsCube.size());
            job.position   = light.position;
            job.range      = std::max(light.radius, 1.0f);
            buildCubeFaceMatrices(job.position, job.range, job.faceMatrices);
            jobsCube.push_back(job);
        }
    }
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

    collectCasters(view, m_jobs2D, m_jobsCube);

    // Push caster descriptors into the UBO - this is what the PBR shader reads.
    shadowData.clear();

    for (const auto& job : m_jobs2D) {
        ShadowCasterGPUData entry;
        entry.lightSpace = job.lightSpace;
        entry.params = glm::vec4(
            static_cast<float>(job.lightIndex),
            static_cast<float>(job.layer),
            m_bias2D,
            0.0f
        );
        shadowData.addCaster(entry);
    }
    for (const auto& job : m_jobsCube) {
        ShadowCasterGPUData entry;
        // lightSpace is unused on the sampling side for cubes - the shader
        // derives its lookup vector from (FragPos - lightPos).
        entry.params = glm::vec4(
            static_cast<float>(job.lightIndex),
            // Encode cube index with a sentinel sign so the PBR shader can
            // tell 2D layers (>=0) from cube indices (<0). We store -(idx+1).
            -(static_cast<float>(job.cubeIndex) + 1.0f),
            m_biasCube,
            job.range
        );
        shadowData.addCaster(entry);
    }

    shadowData.upload();
    shadowData.bind();

    if (m_jobs2D.empty() && m_jobsCube.empty()) return;

    auto& batcher        = glView.getInstanceBatcher();
    const auto& batches  = batcher.getBatches();
    auto& instanceBuffer = batcher.getBuffer();

    // Snapshot GL state we change so we can restore it for downstream passes.
    GLint  prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    const GLboolean prevCullEnabled = glIsEnabled(GL_CULL_FACE);
    GLint  prevCullMode;
    glGetIntegerv(GL_CULL_FACE_MODE, &prevCullMode);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

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

    // ---- 2D casters (directional + spot) -----------------------------------
    if (!m_jobs2D.empty()) {
        // Front-face culling on closed meshes pushes captured depth to the
        // back side, hiding self-shadow acne for directional/spot.
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);

        m_shader2D.bind();
        STATS_RECORD_SHADER_SWITCH();

        for (const auto& job : m_jobs2D) {
            atlas.bind2DLayerForWriting(job.layer);
            m_shader2D.setUniformMatrix4fv(GLConfig::UniformNames::LightSpace, job.lightSpace);
            drawShadowBatches();
        }
    }

    // ---- Cube casters (point lights) ---------------------------------------
    if (!m_jobsCube.empty()) {
        // Point shadows use back-face culling - the linear-distance fragment
        // shader writes the same value regardless of facing, and culling the
        // far side is a cheap perf win.
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        m_shaderCube.bind();
        STATS_RECORD_SHADER_SWITCH();

        for (const auto& job : m_jobsCube) {
            m_shaderCube.setUniform3fv(GLConfig::UniformNames::LightPosition, job.position);
            m_shaderCube.setUniform1f (GLConfig::UniformNames::LightRange,    job.range);

            for (uint32_t face = 0; face < 6; ++face) {
                atlas.bindCubeFaceForWriting(job.cubeIndex, face);
                m_shaderCube.setUniformMatrix4fv(GLConfig::UniformNames::LightSpace, job.faceMatrices[face]);
                drawShadowBatches();
            }
        }
    }

    atlas.unbindForWriting();

    // Restore state.
    glCullFace(static_cast<GLenum>(prevCullMode));
    if (!prevCullEnabled) glDisable(GL_CULL_FACE);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

} // namespace Engine
