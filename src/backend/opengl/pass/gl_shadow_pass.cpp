#include "gl_shadow_pass.h"

#include <cmath>
#include <algorithm>

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>

#include "logger.h"
#include "debug/statistics.h"

#include "core/gl_backend.h"
#include "config/gl_config.h"

#include "resource/gl_mesh.h"
#include "resource/gl_shader_program.h"
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

// FNV-1a over the only inputs that change shadow-map content: the camera
// (CSM cascades are fit to its frustum), every shadow-casting light's
// transform/params, and every shadow-casting drawable's world matrix + mesh.
// Light color/intensity and material do not affect depth, so they are out.
// Assumes transform-based animation (model matrix captures motion); per-vertex
// / skinned deformation would need a frame tag here.
void hashBytes(uint64_t& h, const void* data, size_t n) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
}

uint64_t shadowSignature(const RenderView& view) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a offset basis
    hashBytes(h, &view.camera.view,       sizeof(glm::mat4));
    hashBytes(h, &view.camera.projection, sizeof(glm::mat4));
    for (const auto& l : view.lights) {
        if (l.shadowSlot < 0) continue;
        const int t = static_cast<int>(l.type);
        hashBytes(h, &t,              sizeof(t));
        hashBytes(h, &l.position,     sizeof(glm::vec3));
        hashBytes(h, &l.rotation,     sizeof(glm::quat));
        hashBytes(h, &l.radius,       sizeof(float));
        hashBytes(h, &l.shadowBias,   sizeof(float));
        hashBytes(h, &l.shadowExtent, sizeof(float));
        hashBytes(h, &l.shadowSlot,   sizeof(int));
    }
    // Hash the shadow-caster set (full scene), NOT the camera-culled
    // drawables - otherwise the skip would miss off-screen caster motion
    // and re-render needlessly on every camera pan.
    for (const auto& d : view.shadowCasters) {
        hashBytes(h, &d.model, sizeof(glm::mat4));
        const uint32_t mid = d.mesh.id();
        hashBytes(h, &mid, sizeof(mid));
    }
    return h;
}

// Stable cascaded shadow matrices for a directional light.
//
// Splits the camera frustum with a practical (log/uniform blend) scheme out
// to `maxShadowDist`, fits each cascade with a bounding sphere (rotation- and
// motion-stable, no shimmer), and texel-snaps the result. `maxShadowDist`
// comes from the Light's shadowExtent so a single knob still drives it.
void computeCascades(
    const glm::mat4& camView,
    const glm::mat4& camProj,
    const glm::vec3& lightDir,
    float maxShadowDist,
    uint32_t resolution,
    glm::mat4 out[Config::NumCascades]
) {
    const float p00 = camProj[0][0];
    const float p11 = camProj[1][1];
    const float A   = camProj[2][2];
    const float B   = camProj[3][2];

    const float nearZ  = B / (A - 1.0f);
    const float farZ   = B / (A + 1.0f);
    const float aspect = p11 / p00;
    const float fovy   = 2.0f * std::atan(1.0f / p11);

    const float csmFar = std::min(farZ, std::max(maxShadowDist, nearZ + 1.0f));

    const float lambda = 0.6f;
    float splits[Config::NumCascades];
    for (uint32_t i = 0; i < Config::NumCascades; ++i) {
        const float p    = static_cast<float>(i + 1) / static_cast<float>(Config::NumCascades);
        const float logS = nearZ * std::pow(csmFar / nearZ, p);
        const float linS = nearZ + (csmFar - nearZ) * p;
        splits[i] = lambda * logS + (1.0f - lambda) * linS;
    }

    const glm::vec3 ld = glm::normalize(lightDir);
    const glm::vec3 up = (std::abs(ld.y) > 0.99f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);

    float prev = nearZ;
    for (uint32_t c = 0; c < Config::NumCascades; ++c) {
        const float zn = prev;
        const float zf = splits[c];
        prev = zf;

        const glm::mat4 invVP = glm::inverse(glm::perspective(fovy, aspect, zn, zf) * camView);

        glm::vec3 corners[8];
        int idx = 0;
        for (int x = 0; x < 2; ++x)
            for (int y = 0; y < 2; ++y)
                for (int z = 0; z < 2; ++z) {
                    const glm::vec4 pt = invVP * glm::vec4(
                        x ? 1.0f : -1.0f, y ? 1.0f : -1.0f, z ? 1.0f : -1.0f, 1.0f);
                    corners[idx++] = glm::vec3(pt) / pt.w;
                }

        glm::vec3 center(0.0f);
        for (const auto& cr : corners) center += cr;
        center /= 8.0f;

        float radius = 0.0f;
        for (const auto& cr : corners) radius = std::max(radius, glm::length(cr - center));
        radius = std::ceil(radius * 16.0f) / 16.0f;  // quantize for stability

        const glm::mat4 lightView = glm::lookAt(center - ld * (radius * 2.0f), center, up);
        const float     zext      = radius * 6.0f;   // capture occluders behind the cascade
        const glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, -zext, zext);

        glm::mat4 vp = lightProj * lightView;

        // Texel snap: round the projected origin to the shadow-map grid so
        // cascade edges do not shimmer as the camera moves.
        const glm::vec4 originH = vp * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        const float texScale = static_cast<float>(resolution) * 0.5f;
        const glm::vec2 origin  = glm::vec2(originH) * texScale;
        const glm::vec2 rounded = glm::round(origin);
        const glm::vec2 offset  = (rounded - origin) / texScale;
        vp[3][0] += offset.x;
        vp[3][1] += offset.y;

        out[c] = vp;
    }
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

GLShadowPass::GLShadowPass(ShaderHandle depthShader)
    : RenderPass("GLShadowPass")
    , m_depthShader(depthShader)
{
}

void GLShadowPass::onResize(RenderBackend& /*backend*/, uint32_t /*width*/, uint32_t /*height*/) {
    // Shadow map resolution is fixed and independent of the window size.
}

void GLShadowPass::execute(RenderGraphContext& rg) {
    RenderBackend& backend = rg.backend;
    const RenderView& view = rg.view;
    const ResourceManager& resources = rg.resources;
    if (backend.getType() != RenderBackendType::OpenGL) {
        LOG_ERROR("GLShadowPass requires OpenGL backend, got %s - skipping pass", toString(backend.getType()));
        return;
    }

    // Skip the whole pass when nothing that changes a shadow map moved. The
    // first frame (m_havePrev == false) always renders so the atlas + UBO are
    // populated before any later skip relies on them.
    const uint64_t sig = shadowSignature(view);
    if (m_havePrev && sig == m_lastSig) return;

    auto& gl         = static_cast<GLBackend&>(backend);
    auto& glView     = gl.getView();
    auto& atlas      = glView.getShadowAtlas();
    auto& shadowData = glView.getShadowData();

    GLShader* shader = glView.resolveShader(m_depthShader, resources);
    if (!shader) {
        LOG_ERROR("GLShadowPass: shader handle could not be resolved");
        return;
    }

    shadowData.clear();

    // Shadow casters come from the dedicated full-scene batcher, not the
    // camera-culled forward batcher.
    auto& batcher        = glView.getShadowBatcher();
    const auto& batches  = batcher.getBatches();

    auto& ctx = gl.getContext();
    const bool   prevCullEnabled = ctx.isFaceCullingEnabled();
    const GLenum prevCullFace    = ctx.getCullFace();

    ctx.setDepthTest(true);
    ctx.setDepthWrite(true);
    // Front-face culling pushes captured depth to the back of each closed
    // mesh, hiding self-shadow acne for 2D and cube targets alike.
    ctx.setFaceCulling(true);
    ctx.setCullFace(GL_FRONT);

    shader->bind();
    STATS_RECORD_SHADER_SWITCH();

    auto drawShadowBatches = [&]() {
        for (const auto& batch : batches) {
            if (batch.materialType != MaterialType::Opaque) continue;
            if (batch.instanceCount == 0) continue;

            GLMesh* mesh = glView.getMutableMesh(batch.mesh);
            if (!mesh) continue;

            batcher.attachToVAO(*mesh->getVAO(), GLConfig::InstanceAttributes::ModelMatrixStart);
            mesh->drawInstancedBaseInstance(GL_TRIANGLES, batch.instanceCount, batch.firstInstance);
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
                shader->setUniformMatrix4fv(GLConfig::UniformNames::LightSpace, cubeFaces[face]);
                drawShadowBatches();
            }
        } else if (light.type == LightType::Directional) {
            glm::mat4 cascades[Config::NumCascades];
            computeCascades(view.camera.view, view.camera.projection,
                            Math::computeForward(light.rotation),
                            light.shadowExtent, GLConfig::Limits::ShadowResolution2D,
                            cascades);

            for (uint32_t c = 0; c < Config::NumCascades; ++c) {
                const uint32_t layer = slot + c;

                Shadow2DCasterGPU entry;
                entry.lightSpace = cascades[c];
                entry.params     = glm::vec4(light.shadowBias, 0.0f, 0.0f, 0.0f);
                shadowData.setCaster2D(layer, entry);

                atlas.bind2DLayerForWriting(layer);
                shader->setUniformMatrix4fv(GLConfig::UniformNames::LightSpace, cascades[c]);
                drawShadowBatches();
            }
            shadowData.setCSM(static_cast<int>(slot), static_cast<int>(Config::NumCascades));
            count2D += Config::NumCascades;
        } else {
            const glm::mat4 lightSpace = spotLightSpace(light.position, light.rotation,
                                                        light.outerConeAngle, light.radius);

            Shadow2DCasterGPU entry;
            entry.lightSpace = lightSpace;
            entry.params     = glm::vec4(light.shadowBias, 0.0f, 0.0f, 0.0f);
            shadowData.setCaster2D(slot, entry);
            ++count2D;

            atlas.bind2DLayerForWriting(slot);
            shader->setUniformMatrix4fv(GLConfig::UniformNames::LightSpace, lightSpace);
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

    m_lastSig  = sig;
    m_havePrev = true;
}

} // namespace Engine
