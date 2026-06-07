#define VKM_LOG_CATEGORY "EDITOR"

#include "framework/material_preview_session.h"

#include <glm/gtx/quaternion.hpp>   // glm::rotation (GLM_ENABLE_EXPERIMENTAL is project-wide)
#include <glm/gtc/matrix_transform.hpp>

#include "logger.h"

#include "debug/profiler.h"
#include "ecs/component/light.h"
#include "resource/resource_manager.h"
#include "system/render/frame_resources.h"
#include "system/render/render_backend.h"
#include "system/render/render_graph.h"
#include "system/render/render_pass.h"
#include "system/render/render_system.h"
#include "system/render/render_target.h"

namespace Engine {

MaterialPreviewSession::MaterialPreviewSession(RenderSystem& renderSystem, uint32_t size)
    : m_renderSystem(renderSystem)
    , m_size(size)
{}

MaterialPreviewSession::~MaterialPreviewSession() = default;

void MaterialPreviewSession::onFrameBegin() {
    m_budget = BUDGET_PER_FRAME;
}

void MaterialPreviewSession::evict(uint64_t key) {
    m_targets.erase(key);
    m_textureIds.erase(key);
    m_versions.erase(key);
}

void MaterialPreviewSession::clear() {
    m_targets.clear();
    m_textureIds.clear();
    m_versions.clear();
}

uint32_t MaterialPreviewSession::cachedTextureId(uint64_t key) const {
    auto it = m_textureIds.find(key);
    return it != m_textureIds.end() ? it->second : 0u;
}

uint32_t MaterialPreviewSession::texture(
    ResourceManager& resources,
    const MaterialHandle& material,
    const MeshHandle& mesh,
    float yawDeg,
    float pitchDeg,
    float distance,
    uint64_t key,
    uint64_t version,
    bool live
) {
    if (!material || !mesh || m_size == 0) return 0;

    // Change-detection for BOTH paths: when the cached image already matches
    // this version, skip the render entirely. This is what keeps an open
    // Material Editor from re-running the whole render graph every idle frame -
    // the live preview only re-bakes when the inputs the caller folded into
    // `version` (material edit, orbit camera, preview shape) actually change.
    auto vIt = m_versions.find(key);
    const uint32_t cached = cachedTextureId(key);
    const bool fresh = vIt != m_versions.end()
        && vIt->second == version
        && cached != 0;
    if (fresh) return cached;

    // Non-live thumbnails additionally amortise first-time bakes across frames
    // via a per-frame budget; the live preview renders as soon as it changes.
    if (!live) {
        if (m_budget == 0) return cached;
        --m_budget;
    }

    // Defer the actual render to the top of the next executeFrame (via the
    // backend-job queue), so it runs before that frame's scene render. The
    // texture id we return for this frame is whatever's currently cached - 0
    // on the very first frame for a key, a stable id thereafter.
    m_renderSystem.queueBackendJob(
        [this, &resources, material, mesh, yawDeg, pitchDeg, distance, key]() {
            renderPreview(resources, material, mesh,
                          yawDeg, pitchDeg, distance, key);
        });
    m_versions[key] = version;
    return cachedTextureId(key);
}

void MaterialPreviewSession::renderPreview(
    ResourceManager& resources,
    const MaterialHandle& material,
    const MeshHandle& mesh,
    float yawDeg,
    float pitchDeg,
    float distance,
    uint64_t key
) {
    PROFILE_SCOPE("MaterialPreviewSession::render");

    RenderBackend& backend = m_renderSystem.getBackend();

    // Lazy first-touch allocation; runs inside executeFrame with the GL context
    // current.
    if (!m_frame) {
        m_frame = backend.createFrameResources();
        if (!m_frame) return;
        m_frame->resize(m_size, m_size);
    }

    RenderTarget* target = nullptr;
    {
        auto it = m_targets.find(key);
        if (it == m_targets.end()) {
            auto fresh = backend.createOffscreenTarget(m_size);
            if (!fresh) return;
            const uint32_t id = fresh->getColorTexture();
            target = fresh.get();
            m_textureIds[key] = id;
            m_targets.emplace(key, std::move(fresh));
        } else {
            target = it->second.get();
        }
    }
    if (!target) return;

    // Studio camera orbiting the origin.
    const float yaw   = glm::radians(yawDeg);
    const float pitch = glm::radians(pitchDeg);
    const glm::vec3 eye = distance * glm::vec3(
        glm::cos(pitch) * glm::sin(yaw),
        glm::sin(pitch),
        glm::cos(pitch) * glm::cos(yaw));

    RenderView& v = m_view;
    v.drawables.clear();
    v.lights.clear();

    v.camera.view           = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    v.camera.projection     = glm::perspective(glm::radians(40.0f), 1.0f, 0.05f, 100.0f);
    v.camera.viewProjection = v.camera.projection * v.camera.view;
    v.camera.position       = eye;
    v.camera.exposure       = 1.0f;

    // The scene's environment (IBL bake / SSR / bloom all match the viewport),
    // but with view-dependent / temporal effects forced off so a static
    // material ball reads as a clean, stable studio shot.
    v.environment                       = m_renderSystem.getEnvironment();
    v.environment.exposure.autoExposure = false;
    v.environment.taa.enabled           = false;
    v.environment.dof.enabled           = false;
    v.environment.motionBlur.enabled    = false;
    v.environment.grid.enabled      = false;

    v.viewportWidth  = m_size;
    v.viewportHeight = m_size;
    v.deltaTime      = 0.0f;

    DrawableData d;
    d.mesh         = mesh;
    d.material     = material;
    d.materialType = resources.get(material).type;
    d.castShadows  = false;
    d.model        = glm::mat4(1.0f);
    v.drawables.emplace_back(d);

    LightData lightKey{};
    lightKey.type        = LightType::Directional;
    lightKey.color       = glm::vec3(1.0f);
    lightKey.intensity   = 2.5f;
    lightKey.castShadows = false;
    lightKey.position    = glm::vec3(0.0f);
    lightKey.rotation    = glm::rotation(
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::normalize(glm::vec3(-0.5f, -0.7f, -0.6f)));
    lightKey.shadowSlot  = -1;
    v.lights.emplace_back(lightKey);

    RenderGraph& graph = m_renderSystem.getGraph();

    // Hand-built RenderView can slip past sync's version-gating heuristics;
    // force every mesh/material/texture in @p view resident first.
    backend.ensureResourcesResident(v, resources);

    graph.pushFrameResources(*m_frame, *target);
    backend.syncResources(v, resources);
    graph.execute(backend, v, resources);
    graph.popFrameResources();

    // The composite pass left the offscreen FBO bound. Rebind the default
    // target so any subsequent backend work this frame (ImGui submit)
    // doesn't render into the preview texture.
    backend.getDefaultTarget().bind();

    (void)key;
}

} // namespace Engine
