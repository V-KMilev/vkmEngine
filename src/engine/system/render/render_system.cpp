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

void RenderSystem::update(FrameContext& /*ctx*/) {
    // Engine drives buildView() + executeFrame() separately around the
    // render-thread sync point. This override exists because System::
    // update is pure virtual; it is intentionally never called.
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
        view.build(ctx.scene, ctx.resources, *ctx.visibility, ctx.viewportWidth, ctx.viewportHeight, m_shadowCache);
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

void RenderSystem::queueBackendJob(std::function<void()> job) {
    if (!job) return;
    std::lock_guard<std::mutex> lock(m_pendingBackendJobsMutex);
    m_pendingBackendJobs.push_back(std::move(job));
}

void RenderSystem::requestIBLRebake() {
    // Touch the backend's IBL state on the render thread (where the bake
    // pass reads it), not from the editor's UI thread. The job runs at the
    // top of the next executeFrame, before the IBL bake pass.
    queueBackendJob([this]() { getBackend().requestIBLRebake(); });
}

} // namespace Engine
