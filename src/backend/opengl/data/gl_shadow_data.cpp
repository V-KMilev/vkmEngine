#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_shadow_data.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "gl_uniform_buffer.h"

#include "convention/gl_bindings.h"
#include "data/gl_shadow_atlas.h"
#include "data/gl_cubemap.h"
#include "data/gl_ubo_util.h"
#include "ecs/component/light.h"
#include "system/render/render_view.h"

namespace Engine {

GLShadowData::GLShadowData() {
    for (int& s : m_lightSlot) s = -1;
}
GLShadowData::~GLShadowData() = default;

namespace {

// Range to use for a light that carries no radius.
constexpr float DEFAULT_LIGHT_RANGE = 50.0f;

// A stable up axis for a light/view direction (avoids a degenerate lookAt when
// the direction is near-vertical).
glm::vec3 stableUp(const glm::vec3& dir) {
    return std::abs(dir.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
}

} // namespace

void GLShadowData::build(const RenderView& view) {
    m_jobs2D.clear();
    m_jobsCube.clear();
    m_data = ShadowUBOData{};
    for (int& s : m_lightSlot) s = -1;
    m_lightCount = std::min<uint32_t>(static_cast<uint32_t>(view.lights.size()), SHADOW_MAX_TRACKED_LIGHTS);

    // Mirror the atlas tile resolution the backend rebuilt for this frame, so the
    // normal-offset bias's world-texel size below matches the actual atlas.
    m_shadowRes = view.settings.shadowResolution;

    // Camera frustum corners in world space, from the inverse view-projection.
    const glm::mat4 invVP = glm::inverse(view.camera.projection * view.camera.view);
    const glm::vec2 ndc[4] = { {-1, -1}, {1, -1}, {1, 1}, {-1, 1} };
    CameraFrustum cam;
    for (int k = 0; k < 4; ++k) {
        glm::vec4 n = invVP * glm::vec4(ndc[k].x, ndc[k].y, -1.0f, 1.0f);
        glm::vec4 f = invVP * glm::vec4(ndc[k].x, ndc[k].y,  1.0f, 1.0f);
        cam.nearCorners[k] = glm::vec3(n) / n.w;
        cam.farCorners[k]  = glm::vec3(f) / f.w;
    }
    const glm::vec3 camPos = view.camera.position;
    glm::vec3 nearCenter(0.0f), farCenter(0.0f);
    for (int k = 0; k < 4; ++k) { nearCenter += cam.nearCorners[k]; farCenter += cam.farCorners[k]; }
    nearCenter *= 0.25f;
    farCenter  *= 0.25f;

    const glm::vec3 camFwd = glm::normalize(farCenter - nearCenter);
    cam.nearDepth = glm::dot(nearCenter - camPos, camFwd);
    cam.farDepth  = glm::dot(farCenter  - camPos, camFwd);
    if (cam.nearDepth < 0.01f) cam.nearDepth = 0.01f;
    if (cam.farDepth  <= cam.nearDepth) cam.farDepth = cam.nearDepth + 1.0f;

    m_data.camForward = glm::vec4(camFwd, 0.0f);

    // One pass over the shadow casters, each fitted into the next free atlas
    // slot(s) for its light type.
    uint32_t next2D   = 0;
    uint32_t nextCube = 0;
    bool     haveSun  = false;

    for (uint32_t i = 0; i < m_lightCount; ++i) {
        const LightData& light = view.lights[i];
        if (!light.castShadows) continue;

        switch (light.type) {
            case LightType::Directional: fitDirectional(light, i, cam, next2D, haveSun); break;
            case LightType::Spot:        fitSpot(light, i, next2D);                      break;
            case LightType::Point:       fitPoint(light, i, nextCube);                   break;
            default: break;
        }
    }
}

// Directional sun: N frustum-fit cascades into the first 2D slots.
void GLShadowData::fitDirectional(const LightData& light, uint32_t lightIndex,
                                  const CameraFrustum& cam, uint32_t& next2D, bool& haveSun) {
    // cascadeSplits is a vec4 and fr[] is sized N+1 == 5, so 4 cascades max.
    const uint32_t N = std::min<uint32_t>(Config::NUM_CASCADES, 4u);
    if (haveSun || next2D + N > Config::MAX_SHADOW_CASTERS_2D) return;
    haveSun = true;

    const uint32_t base = next2D;
    m_data.csmBase  = static_cast<int>(base);
    m_data.csmCount = static_cast<int>(N);

    // Cap this sun's cascades to its shadowDistance so they pack tightly instead
    // of spreading over the camera's full far plane. Geometry past sunFar is
    // unshadowed.
    const float sunFar = std::max(cam.nearDepth + 1.0f, std::min(cam.farDepth, light.shadowDistance));

    // Practical split scheme (blend of logarithmic + uniform), expressed as
    // fractions of the full near->far edge so they index the frustum corners
    // directly. The last split caps at sunFar.
    const float lambda = 0.7f;
    float fr[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    for (uint32_t c = 1; c < N; ++c) {
        const float si   = static_cast<float>(c) / static_cast<float>(N);
        const float logS = cam.nearDepth * std::pow(sunFar / cam.nearDepth, si);
        const float uniS = cam.nearDepth + (sunFar - cam.nearDepth) * si;
        const float d    = lambda * logS + (1.0f - lambda) * uniS;
        fr[c] = (d - cam.nearDepth) / (cam.farDepth - cam.nearDepth);
    }
    fr[N] = (sunFar - cam.nearDepth) / (cam.farDepth - cam.nearDepth);

    const glm::vec3 dir = glm::normalize(light.direction);
    const glm::vec3 up  = stableUp(dir);

    for (uint32_t c = 0; c < N; ++c) {
        glm::vec3 corners[8];
        for (int k = 0; k < 4; ++k) {
            corners[k]     = glm::mix(cam.nearCorners[k], cam.farCorners[k], fr[c]);
            corners[k + 4] = glm::mix(cam.nearCorners[k], cam.farCorners[k], fr[c + 1]);
        }
        glm::vec3 center(0.0f);
        for (const glm::vec3& p : corners) center += p;
        center *= 0.125f;

        // Bounding-sphere fit: stable under camera rotation, no shimmer from a
        // light-space AABB. Round the radius to damp it further.
        float radius = 0.0f;
        for (const glm::vec3& p : corners) radius = std::max(radius, glm::length(p - center));
        radius = std::ceil(radius);

        const float     zExtend = radius;  // pull the near plane back to catch occluders
        const glm::vec3 eye     = center - dir * (radius + zExtend);
        const glm::mat4 lView   = glm::lookAt(eye, center, up);
        const glm::mat4 lProj   = glm::ortho(-radius, radius, -radius, radius,
                                             0.0f, 2.0f * radius + zExtend);
        const glm::mat4 lightVP = lProj * lView;

        // World size of one shadow texel in this cascade - drives the shader's
        // normal-offset bias so it scales with cascade density.
        const float worldTexel = (2.0f * radius) / static_cast<float>(m_shadowRes);

        const uint32_t slot = base + c;
        Shadow2DGPU& e = m_data.s2d[slot];
        e.lightVP = lightVP;
        e.atlas   = glm::vec4(GLShadowAtlas::tileUVOffset(slot), GLShadowAtlas::tileUVScale());
        e.params  = glm::vec4(light.shadowBias, worldTexel, 0.0f, 0.0f);
        m_jobs2D.push_back({ lightVP, slot });

        m_data.cascadeSplits[static_cast<int>(c)] =
            cam.nearDepth + fr[c + 1] * (cam.farDepth - cam.nearDepth);
    }
    next2D += N;
    m_lightSlot[lightIndex] = m_data.csmBase;  // any >= 0 flags "this light has a shadow"
}

// Spot: one perspective map into the next free 2D slot.
void GLShadowData::fitSpot(const LightData& light, uint32_t lightIndex, uint32_t& next2D) {
    if (next2D >= Config::MAX_SHADOW_CASTERS_2D) return;
    const uint32_t slot = next2D++;

    const glm::vec3 dir   = glm::normalize(light.direction);
    const glm::vec3 up    = stableUp(dir);
    const float     range = light.radius > 0.0f ? light.radius : DEFAULT_LIGHT_RANGE;
    const float     fov   = std::min(glm::radians(170.0f), 2.0f * light.outerConeAngle * 1.1f);

    const glm::mat4 lView   = glm::lookAt(light.position, light.position + dir, up);
    const glm::mat4 lProj   = glm::perspective(fov, 1.0f, 0.1f, range);
    const glm::mat4 lightVP = lProj * lView;

    // Approximate world texel size at the cone's far end, for normal-offset bias.
    const float worldTexel = (2.0f * range * std::tan(fov * 0.5f)) /
                             static_cast<float>(m_shadowRes);

    Shadow2DGPU& e = m_data.s2d[slot];
    e.lightVP = lightVP;
    e.atlas   = glm::vec4(GLShadowAtlas::tileUVOffset(slot), GLShadowAtlas::tileUVScale());
    e.params  = glm::vec4(light.shadowBias, worldTexel, 0.0f, 0.0f);
    m_jobs2D.push_back({ lightVP, slot });

    m_lightSlot[lightIndex] = static_cast<int>(slot);
}

// Point: six perspective faces into the next free cube slot.
void GLShadowData::fitPoint(const LightData& light, uint32_t lightIndex, uint32_t& nextCube) {
    if (nextCube >= Config::MAX_SHADOW_CASTERS_CUBE) return;
    const uint32_t slot = nextCube++;

    const float range = light.radius > 0.0f ? light.radius : DEFAULT_LIGHT_RANGE;
    const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f,
                                            Config::SHADOW_CUBE_NEAR, range);

    ShadowCubeJob job;
    job.pos   = light.position;
    job.range = range;
    job.slot  = slot;
    for (int f = 0; f < 6; ++f) {
        job.faceVP[f] = proj * GLCubemap::faceView(f, light.position);
    }
    m_jobsCube.push_back(job);

    ShadowCubeGPU& e = m_data.scube[slot];
    e.posRange = glm::vec4(light.position, range);
    e.params   = glm::vec4(light.shadowBias, 0.0f, 0.0f, 0.0f);

    m_lightSlot[lightIndex] = static_cast<int>(slot);
}

int GLShadowData::slotForLight(uint32_t lightIndex) const {
    return lightIndex < m_lightCount ? m_lightSlot[lightIndex] : -1;
}

void GLShadowData::uploadAndBind() {
    uploadUBOIfChanged(m_ubo, m_last, m_data);
    bindUBO(m_ubo, GLBindings::UBOBindingPoints::Shadow);
}

} // namespace Engine
