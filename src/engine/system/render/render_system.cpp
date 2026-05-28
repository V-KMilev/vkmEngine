#define VKM_LOG_CATEGORY "RENDER"

#include "system/render/render_system.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>   // glm::rotation (GLM_ENABLE_EXPERIMENTAL is project-wide)

#include "logger.h"

#include "debug/profiler.h"
#include "resource/resource_manager.h"
#include "ecs/scene.h"
#include "platform/window/window_manager.h"
#include "system/visibility/visibility.h"
#include "system/render/environment.h"

#include "system/render/render_backend.h"
#include "system/render/render_pass.h"
#include "system/render/render_target.h"

namespace Engine {

RenderSystem::RenderSystem() : m_width(0), m_height(0) {}

RenderSystem::~RenderSystem() {
    m_backend.reset();
}

void RenderSystem::setBackend(std::unique_ptr<RenderBackend> backend) {
    if (backend) {
        LOG_INFO("Backend set to '%s'", backend->apiName());
    } else {
        LOG_INFO("Backend cleared");
    }
    m_backend = std::move(backend);
}

void RenderSystem::resize(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;

    if (!m_backend) return;

    m_backend->resize(width, height);
    m_graph.onResize(*m_backend, width, height);
}

void RenderSystem::update(FrameContext& ctx) {
    // Single-threaded path: build then execute against the same buffer.
    // Frame index 0 picks m_views[0]; the buffer isn't shared with any
    // other thread so parity doesn't matter here.
    buildView(ctx, 0);
    executeFrame(ctx, 0);
}

void RenderSystem::buildView(FrameContext& ctx, uint32_t frameIndex) {
    PROFILE_SCOPE("RenderSystem/BuildView");

    if (!m_backend) {
        LOG_WARNING("No backend set, skipping view build");
        return;
    }

    // m_width / m_height are the LAST sizes the backend was resized to.
    // We do not call resize() here: it touches backend state (FBO
    // reallocation), so it must run on the thread holding the backend's
    // context - the render thread under the overlap loop. executeFrame()
    // picks up the desired size from ctx and resizes there if needed.

    // Refill the editor thumbnail bake budget for this frame (consumed during
    // the later UI stage by the Asset Browser).
    m_thumbBudget = THUMB_BUDGET_PER_FRAME;

    RenderView& view = m_views[frameIndex & 1u];

    // Environment lives as a singleton component on a scene entity (editable
    // in the Inspector). Pull it each frame; mirror into m_environment so
    // getEnvironment() returns a stable reference between frames.
    m_environment      = sceneEnvironment(ctx.scene);
    view.environment   = m_environment;
    view.modeConfig    = resolveModeConfig(m_environment.renderMode);
    view.deltaTime     = ctx.deltaTime;

    {
        PROFILE_SCOPE("Render/BuildView");
        view.build(ctx.scene, ctx.resources, *ctx.visibility, ctx.viewportWidth, ctx.viewportHeight);
    }

    view.viewportX    = ctx.viewportX;
    view.viewportY    = ctx.viewportY;
    view.windowWidth  = static_cast<uint32_t>(ctx.window.getWidth());
    view.windowHeight = static_cast<uint32_t>(ctx.window.getHeight());

    if (view.modeConfig.disableSSAO) {
        view.environment.ao.enabled = false;
    }
}

void RenderSystem::executeFrame(FrameContext& ctx, uint32_t frameIndex) {
    PROFILE_SCOPE("RenderSystem/ExecuteFrame");

    if (!m_backend) return;

    // Backend-side resize is allowed here because we're on the thread
    // holding the backend's context. m_width/m_height track the last-
    // applied backend size; ctx carries this frame's request.
    if (ctx.viewportWidth != m_width || ctx.viewportHeight != m_height) {
        m_width  = ctx.viewportWidth;
        m_height = ctx.viewportHeight;
        m_backend->resize(m_width, m_height);
        m_graph.onResize(*m_backend, m_width, m_height);
    }

    // Drain any backend jobs queued from main this frame (material
    // previews from editor panels, future screenshot capture, etc.). They
    // run here, before the scene render + ImGui draw, so any textures
    // they touch contain fresh content by the time ImGui samples them.
    // Swap under the lock, then run outside it so a job can re-queue
    // safely without deadlocking.
    {
        PROFILE_SCOPE("Render/BackendJobs");
        thread_local std::vector<std::function<void()>> scratch;
        scratch.clear();
        {
            std::lock_guard<std::mutex> lock(m_pendingBackendJobsMutex);
            scratch.swap(m_pendingBackendJobs);
        }
        for (auto& job : scratch) job();
    }

    RenderView& view = m_views[frameIndex & 1u];

    {
        PROFILE_SCOPE("Render/SyncResources");
        m_backend->syncResources(view, ctx.resources);
    }

    {
        PROFILE_SCOPE("Render/ExecuteGraph");
        m_graph.execute(*m_backend, view, ctx.resources);
    }
}

void RenderSystem::addPass(std::unique_ptr<RenderPass> pass) {
    LOG_TRACE("AddPass '%s' (now %zu)",
        pass ? pass->getName().c_str() : "<null>", m_graph.passCount() + 1);
    m_graph.addPass(std::move(pass));
}

void RenderSystem::clearPasses() {
    m_graph.clear();
}

void RenderSystem::invalidateTemporalHistory() {
    m_graph.invalidateTemporalHistory();
}

size_t RenderSystem::passCount() const {
    return m_graph.passCount();
}

std::string_view RenderSystem::passName(size_t index) const {
    return m_graph.getPass(index).getName();
}

bool RenderSystem::isPassEnabled(size_t index) const {
    return m_graph.getPass(index).isEnabled();
}

void RenderSystem::setPassEnabled(size_t index, bool enabled) {
    m_graph.getPass(index).setEnabled(enabled);
}

uint32_t RenderSystem::renderMaterialPreview(
    ResourceManager& resources,
    const MaterialHandle& material,
    const MeshHandle& mesh,
    float yawDeg,
    float pitchDeg,
    float distance,
    uint32_t size
) {
    PROFILE_SCOPE("RenderSystem::renderMaterialPreview");

    if (!m_backend || size == 0 || !material || !mesh) return 0;

    // Studio camera orbiting the origin.
    const float yaw   = glm::radians(yawDeg);
    const float pitch = glm::radians(pitchDeg);
    const glm::vec3 eye = distance * glm::vec3(
        glm::cos(pitch) * glm::sin(yaw),
        glm::sin(pitch),
        glm::cos(pitch) * glm::cos(yaw));

    RenderView& v = m_previewView;
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
    v.environment                       = m_environment;
    v.environment.exposure.autoExposure = false;   // deterministic studio exposure
    v.environment.taa.enabled           = false;
    v.environment.dof.enabled           = false;
    v.environment.motionBlur.enabled    = false;

    v.viewportWidth  = size;
    v.viewportHeight = size;
    v.deltaTime      = 0.0f;

    DrawableData d;
    d.mesh         = mesh;
    d.material     = material;
    d.materialType = resources.get(material).type;
    d.castShadows  = false;
    d.model        = glm::mat4(1.0f);
    v.drawables.emplace_back(d);

    // Single directional key light; IBL fills the rest.
    LightData key{};
    key.type           = LightType::Directional;
    key.color          = glm::vec3(1.0f);
    key.intensity      = 2.5f;
    key.radius         = 0.0f;
    key.innerConeAngle = 0.0f;
    key.outerConeAngle = 0.0f;
    key.castShadows    = false;
    key.shadowBias     = 0.0f;
    key.shadowExtent   = 0.0f;
    key.shadowSlot     = -1;
    key.position       = glm::vec3(0.0f);
    key.rotation       = glm::rotation(glm::vec3(0.0f, 0.0f, 1.0f),
                             glm::normalize(glm::vec3(-0.5f, -0.7f, -0.6f)));
    v.lights.emplace_back(key);

    // World-grid and AABB-debug make no sense around a single preview shape;
    // suppress just those two, leave the rest of the pipeline intact.
    const size_t passes = m_graph.passCount();
    for (size_t i = 0; i < passes; ++i) {
        RenderPass& p = m_graph.getPass(i);
        const std::string& n = p.getName();
        if (n == "GLGridPass" || n == "GLAABBDebugPass") {
            m_previewPassWasEnabled.push_back({ i, p.isEnabled() });
            p.setEnabled(false);
        }
    }

    m_graph.beginPreview(*m_backend, size);
    // Preview's hand-built RenderView can slip past sync's version-gating
    // heuristics; force the mesh/material/texture resident first.
    m_backend->ensurePreviewResourceTables(v, resources);
    m_backend->syncResources(v, resources);
    m_graph.execute(*m_backend, v, resources);
    m_graph.endPreview();
    // The composite pass left the preview FBO bound. Rebind the window
    // backbuffer so ImGui (which draws into the active FBO) doesn't end
    // up rendering the editor into the preview texture.
    m_backend->getDefaultTarget().bind();

    for (const auto& s : m_previewPassWasEnabled) {
        m_graph.getPass(s.first).setEnabled(s.second);
    }
    m_previewPassWasEnabled.clear();

    return m_graph.previewColorTexture();
}

uint32_t RenderSystem::materialPreviewTexture(
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
    if (!m_backend) return 0;

    if (!live) {
        const uint32_t cached = m_graph.cachedPreview(*m_backend, key);
        auto it = m_thumbVersion.find(key);
        const bool fresh = cached && it != m_thumbVersion.end() && it->second == version;
        if (fresh) return cached;            // up to date - no work
        if (m_thumbBudget == 0) return cached;  // wait our turn (0 = placeholder)
        --m_thumbBudget;
    }

    // Deferred path: the render thread owns the backend context. Queue a
    // backend job that does the actual render + cache snapshot, return
    // whatever's currently cached (0 the very first frame; a stable
    // per-key texture id thereafter). The generic backend-job queue is
    // drained at the top of executeFrame() before the scene + ImGui
    // draw, so ImGui samples the freshly-rendered content in the same
    // frame the request was made.
    if (m_backendOnSeparateThread) {
        queueBackendJob([this, &resources, material, mesh, yawDeg, pitchDeg, distance, key]() {
            const uint32_t live = renderMaterialPreview(
                resources, material, mesh, yawDeg, pitchDeg, distance, PREVIEW_RES);
            if (!live) return;
            m_graph.snapshotPreviewToCache(*m_backend, key, PREVIEW_RES);
        });
        if (!live) m_thumbVersion[key] = version;  // recorded as "requested at version V"
        return m_graph.cachedPreview(*m_backend, key);
    }

    // Inline path (single-threaded mode): same fixed offscreen resolution
    // for every preview so switching between the 512 Material Editor and
    // small grid thumbnails never reallocates the preview targets mid-frame.
    const uint32_t liveTex =
        renderMaterialPreview(resources, material, mesh,
                              yawDeg, pitchDeg, distance, PREVIEW_RES);
    if (!liveTex) return live ? 0u : m_graph.cachedPreview(*m_backend, key);

    const uint32_t snap = m_graph.snapshotPreviewToCache(*m_backend, key, PREVIEW_RES);
    if (!live) m_thumbVersion[key] = version;
    return snap ? snap : liveTex;
}

void RenderSystem::queueBackendJob(std::function<void()> job) {
    if (!job) return;
    std::lock_guard<std::mutex> lock(m_pendingBackendJobsMutex);
    m_pendingBackendJobs.push_back(std::move(job));
}

} // namespace Engine
