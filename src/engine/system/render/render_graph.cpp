#define VKM_LOG_CATEGORY "RENDER"

#include "system/render/render_graph.h"

#include <algorithm>

#include "l_assert.h"

#include "logger.h"
#include "debug/profiler.h"
#include "system/render/frame_resources.h"
#include "system/render/render_backend.h"
#include "system/render/render_graph_builder.h"
#include "system/render/render_graph_context.h"
#include "system/render/render_pass.h"
#include "system/render/render_target.h"

namespace Engine {

RenderGraph::RenderGraph()  = default;
RenderGraph::~RenderGraph() = default;

RenderPass& RenderGraph::getPass(size_t index) {
    return *m_passes[index];
}

const RenderPass& RenderGraph::getPass(size_t index) const {
    return *m_passes[index];
}

namespace {
bool contains(const std::vector<RGResource>& list, RGResource r) {
    return std::find(list.begin(), list.end(), r) != list.end();
}
}

void RenderGraph::addPass(std::unique_ptr<RenderPass> pass) {
    m_passes.emplace_back(std::move(pass));
    m_compiled = false;
    m_lastEnabled.clear();
}

void RenderGraph::clear() {
    m_passes.clear();
    m_reads.clear();
    m_writes.clear();
    m_compiled = false;
    m_lastEnabled.clear();
    // Backend-owned persistents (ShadowAtlas, IBL) must be re-registered
    // after a clear+repopulate; otherwise the next execute() skips the
    // registerPersistentResources() call and passes see null handles.
    m_persistentRegistered = false;
}

void RenderGraph::onResize(RenderBackend& backend, uint32_t width, uint32_t height) {
    m_width  = width;
    m_height = height;
    // Lazy-create the default pool on first resize; resize the existing pool
    // on subsequent calls. The backend factory may return nullptr while the
    // backend is still mid-setup (rare); guard so we don't crash here.
    FrameResources& f = ensureFrame(backend);
    f.resize(width, height);
    for (auto& pass : m_passes) {
        pass->onResize(backend, width, height);
    }
}

FrameResources& RenderGraph::ensureFrame(RenderBackend& backend) {
    if (!m_frame) {
        m_frame = backend.createFrameResources();
    }
    return *m_frame;
}

FrameResources& RenderGraph::activeFrame() const {
    return m_pushedFrame ? *m_pushedFrame : *m_frame;
}

void RenderGraph::compile(const RenderView* view) {
    // Disabled passes contribute no declarations: their reads/writes
    // don't extend lifetimes and don't appear in the read-before-write
    // validation. When view is null (first compile before any frame),
    // every pass declares and lifetimes are conservative.
    const size_t n = m_passes.size();
    m_reads.assign(n, {});
    m_writes.assign(n, {});
    for (uint32_t i = 0; i < RG_RESOURCE_COUNT; ++i) m_lifetimes[i] = RGResourceLifetime{};

    bool produced[RG_RESOURCE_COUNT] = {};
    bool clean = true;
    size_t activePasses = 0;

    for (size_t i = 0; i < n; ++i) {
        const bool active = view ? m_passes[i]->enabledForView(*view) : m_passes[i]->isEnabled();
        if (!active) continue;
        ++activePasses;

        RenderGraphBuilder builder(m_reads[i], m_writes[i]);
        m_passes[i]->declareResources(builder);

        // Reads are validated against everything produced by earlier passes.
        // In release a warning is enough (the frame still runs, reading stale
        // or zero memory); in debug we trip an assert so the developer sees
        // the structural bug immediately, not three rendered frames later.
        for (RGResource r : m_reads[i]) {
            const uint32_t ri = static_cast<uint32_t>(r);
            m_lifetimes[ri].lastRead = static_cast<int>(i);
            if (!produced[ri] && !rgResourceIsImplicit(r)) {
                LOG_WARNING("RenderGraph: pass '%s' reads %s before any pass writes it",
                    m_passes[i]->getName().c_str(), rgResourceName(r));
                VKM_ASSERT(false, "RenderGraph read-before-write violation");
                clean = false;
            }
        }
        // Then this pass's writes become available to later passes.
        for (RGResource w : m_writes[i]) {
            const uint32_t wi = static_cast<uint32_t>(w);
            if (m_lifetimes[wi].firstWrite < 0) m_lifetimes[wi].firstWrite = static_cast<int>(i);
            produced[wi] = true;
        }
    }

    uint32_t usedResources = 0;
    for (uint32_t i = 0; i < RG_RESOURCE_COUNT; ++i) {
        if (m_lifetimes[i].used()) ++usedResources;
    }

    // VERBOSE because the editor's material-preview path toggles passes per
    // frame (disables the grid for the preview, re-enables it for the main
    // view), so this fires twice every frame the Material Editor or
    // Asset Browser is open. The signal is still here for anyone watching
    // verbose logs; INFO would just be spam.
    LOG_VERBOSE("RenderGraph compiled: %zu/%zu passes active, %u transient resources%s",
        activePasses, n, usedResources, clean ? "" : " (with validation warnings)");

    // Mark compiled so we don't redo this work every frame. addPass() /
    // clear() flip the flag back to false to force a re-compile, and
    // execute() recompiles when the enable-vector changes.
    m_compiled = true;

    // Cache the enable state we just compiled against. execute() compares
    // each frame and recompiles only when it changes.
    if (view) {
        m_lastEnabled.assign(n, false);
        for (size_t i = 0; i < n; ++i) {
            m_lastEnabled[i] = m_passes[i]->enabledForView(*view);
        }
    } else {
        m_lastEnabled.clear();
    }
}

void RenderGraph::execute(
    RenderBackend& backend,
    const RenderView& view,
    const ResourceManager& resources
) {
    // First compile is structural (pass set just changed); subsequent
    // compiles are triggered by enable-state diffs so lifetimes reflect
    // the schedule that's actually going to run.
    if (!m_compiled) {
        compile(&view);
    } else {
        bool enableChanged = m_lastEnabled.size() != m_passes.size();
        for (size_t i = 0; !enableChanged && i < m_passes.size(); ++i) {
            if (m_lastEnabled[i] != m_passes[i]->enabledForView(view)) enableChanged = true;
        }
        if (enableChanged) compile(&view);
    }

    // Refresh the resource pool from the active FrameResources (default or
    // pushed). registerWith() re-publishes every sub-resource into the
    // typed pool, so a temporary push just needs an extra call into this
    // path.
    FrameResources& f = m_pushedFrame ? *m_pushedFrame : ensureFrame(backend);
    f.registerWith(*this);

    // Persistent resources (ShadowAtlas, IBL) are published once - they
    // outlive the frame and don't change across pushed pools.
    if (!m_persistentRegistered) {
        backend.registerPersistentResources(*this);
        m_persistentRegistered = true;
    }

    // Backbuffer routes through the typed pool too. Push sessions route
    // it at the caller's offscreen target; otherwise it's the window
    // backbuffer the backend hands us.
    RenderTarget* target = m_pushedTarget
        ? m_pushedTarget
        : &backend.getDefaultTarget();
    registerResource(RGResource::Backbuffer, target);

    RenderGraphContext ctx{ backend, view, resources, *this, m_frameIndex++ };

    // The graph owns the MSAA->single-sample resolve. sceneResolveDirty ==
    // "SceneHDRResolved is stale w.r.t. SceneHDR" and is driven explicitly
    // by what each pass writes:
    //   - write(SceneHDR)         -> resolved buffer is now stale
    //   - write(SceneHDRResolved) -> pass authored the resolved buffer
    //                                 directly; it's authoritative until
    //                                 the next SceneHDR write
    //   - resolve()               -> resolved buffer is fresh
    // Passes no longer self-resolve. New passes that write SceneHDR between
    // a TAA/DoF/MB run and composite are handled correctly here without any
    // pass-side change.
    bool sceneResolveDirty = false;

    for (size_t i = 0; i < m_passes.size(); ++i) {
        RenderPass& pass = *m_passes[i];
        // enabledForView() lets each pass key off RenderView state (env
        // toggles, render mode, etc.) without a central pass-name switch.
        // Default returns isEnabled() so passes that don't care behave as
        // before.
        if (!pass.enabledForView(view)) continue;

        if (sceneResolveDirty && contains(m_reads[i], RGResource::SceneHDRResolved)) {
            f.resolveSceneColor();
            sceneResolveDirty = false;
        }

#ifndef NDEBUG
        ctx.accessedResources = 0;
#endif
        {
            // CPU zone only; GPU-side per-pass timing comes from Tracy's GPU
            // zones (PROFILE_GPU_SCOPE_NAMED in the GL pass base) or RenderDoc.
            PROFILE_SCOPE_NAMED(pass.getName().c_str());
            pass.execute(ctx);
        }
#ifndef NDEBUG
        // Compare declared reads+writes against what the pass actually
        // looked up via ctx.resource<T>(). Drift in either direction is
        // a one-shot warning (per-pass + per-resource pair) so a real
        // bug shows up but a busy log doesn't drown the signal.
        checkPassAccess(i, ctx.accessedResources);
#endif

        if (contains(m_writes[i], RGResource::SceneHDR)) {
            sceneResolveDirty = true;
        }
        if (contains(m_writes[i], RGResource::SceneHDRResolved)) {
            sceneResolveDirty = false;
        }
    }

    // Per-frame backend tail: GL backend drains completed Tracy GPU query
    // results here; other backends (if any) no-op. Stays an abstract hook
    // so RenderGraph never needs to include GL headers.
    backend.endFrame();
}

#ifndef NDEBUG
void RenderGraph::checkPassAccess(size_t passIndex, uint32_t accessedMask) {
    // Build a declared bitmask from m_reads[i] + m_writes[i] and compare.
    // Two drifts to warn about:
    //   declared & !accessed -> pass said it would touch X but didn't
    //   accessed & !declared -> pass touched X without declaring it
    // Each (pass, resource, direction) gets warned exactly once per
    // process lifetime - a one-shot std::set guards the log noise.
    uint32_t declaredMask = 0;
    for (RGResource r : m_reads[passIndex])  declaredMask |= (1u << static_cast<uint32_t>(r));
    for (RGResource r : m_writes[passIndex]) declaredMask |= (1u << static_cast<uint32_t>(r));

    const uint32_t declaredOnly = declaredMask & ~accessedMask;
    const uint32_t accessedOnly = accessedMask & ~declaredMask;
    if (declaredOnly == 0 && accessedOnly == 0) return;

    const std::string& passName = m_passes[passIndex]->getName();

    for (uint32_t r = 0; r < RG_RESOURCE_COUNT; ++r) {
        const uint32_t bit = (1u << r);
        if (declaredOnly & bit) {
            // The graph already tolerates "declared but unused" - lifetimes
            // still get extended. Worth a one-shot warn so a pass author
            // notices stale declarations after a refactor.
            const uint64_t key = (static_cast<uint64_t>(passIndex) << 8) | r;
            if (m_accessWarnDeclaredUnused.insert(key).second) {
                LOG_WARNING("RenderGraph: pass '%s' declared %s but never looked it up via ctx.resource<>()",
                    passName.c_str(), rgResourceName(static_cast<RGResource>(r)));
            }
        }
        if (accessedOnly & bit) {
            const uint64_t key = (static_cast<uint64_t>(passIndex) << 8) | r;
            if (m_accessWarnUndeclared.insert(key).second) {
                LOG_WARNING("RenderGraph: pass '%s' looked up %s without declaring it (declareResources missing a read/write)",
                    passName.c_str(), rgResourceName(static_cast<RGResource>(r)));
            }
        }
    }
}
#endif

void RenderGraph::pushFrameResources(FrameResources& pool, RenderTarget& target) {
    m_pushedFrame  = &pool;
    m_pushedTarget = &target;
}

void RenderGraph::popFrameResources() {
    m_pushedFrame  = nullptr;
    m_pushedTarget = nullptr;
}

void RenderGraph::invalidateTemporalHistory() {
    if (m_frame) m_frame->invalidateTemporalHistory();
}

} // namespace Engine
