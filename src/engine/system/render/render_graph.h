#pragma once

#include <memory>
#include <unordered_set>
#include <vector>
#include <cstdint>

#include "system/render/render_graph_resource.h"

namespace Engine {

class RenderPass;       // defined in render_pass.h
class RenderBackend;    // defined in render_backend.h
struct RenderView;      // defined in render_view.h
class ResourceManager;
class FrameResources;   // defined in frame_resources.h
class RenderTarget;     // defined in render_target.h
}

namespace Engine {

/**
 * @brief First write / last read pass indices for a transient resource.
 *
 * Drives future pool aliasing (two resources with disjoint lifetimes can
 * share storage) and the editor debug view. -1 means never written / read.
 */
struct RGResourceLifetime {
    int firstWrite = -1;
    int lastRead   = -1;

    bool used() const { return firstWrite >= 0 || lastRead >= 0; }
};

/**
 * @brief Typed render graph - the pass list, the resource flow, and the
 *        graph-owned transient resource pool.
 *
 * Owns:
 *   - The pass list and their execution order.
 *   - The compile-time validation of resource ordering + per-resource
 *     lifetime intervals (firstWrite -> lastRead).
 *   - The default transient resource pool (FrameResources). External
 *     callers can temporarily override the active pool + target via
 *     pushFrameResources / popFrameResources so they can render the
 *     same graph against their own offscreen target (editor previews,
 *     future capture / reflection-probe baking, etc.) without forcing
 *     the graph to know about those use cases.
 *
 * Doesn't own:
 *   - The concrete GL objects inside the pool (GLFrameResources allocates
 *     them; the graph just holds the abstract handle and forwards
 *     resize / registerWith / resolveSceneColor).
 *   - The GLView and the window backbuffer (still backend-owned).
 *
 * Aliasing analysis (descriptor-aware):
 *   compile() groups resources whose lifetimes are disjoint AND whose
 *   descriptors (shape, format, samples - see rgResourceDescriptor) match.
 *   aliasGroup()/aliasGroupCount() expose the result for the visualizer
 *   and for a future pool refactor. Each member of a group could legally
 *   point at the same physical GL object; the actual sharing requires
 *   GLFrameResources to be reworked to allocate by descriptor signature
 *   instead of one wrapper per RGResource - deferred at the current scale
 *   (~15 transient resources, ~50-100 MB potential savings, 4 GiB+ budget
 *   available) per the trade-off in docs/misc/gaps.md. Revisit if mobile
 *   or memory-constrained targets land on the roadmap.
 */
class RenderGraph {
    public:
        RenderGraph();
        ~RenderGraph();

        RenderGraph(const RenderGraph& other) = delete;
        RenderGraph& operator=(const RenderGraph& other) = delete;

        RenderGraph(RenderGraph && other) = delete;
        RenderGraph& operator=(RenderGraph && other) = delete;

    public:
        /**
         * @brief Append a pass to the end of the schedule. Takes ownership.
         *
         * Marks the graph dirty so compile() re-runs on the next execute().
         */
        void addPass(std::unique_ptr<RenderPass> pass);

        /**
         * @brief Drop every pass + cached declarations.
         *
         * Also resets m_persistentRegistered so the next execute() re-asks
         * the backend for ShadowAtlas / IBL handles (they're tied to the
         * pass set's expectations).
         */
        void clear();

        /**
         * @brief Resize the pool's transient resources to the new viewport.
         *
         * Lazy-allocates the default FrameResources via the backend factory
         * on first call. Forwards onResize() to every pass after the pool
         * is reshaped, so passes that hold viewport-sized state of their
         * own pick up the new dimensions.
         */
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height);

        void execute(RenderBackend& backend, const RenderView& view, const ResourceManager& resources);

        /**
         * @brief Collect declarations, validate ordering, compute lifetimes.
         *
         * When @p view is non-null, passes that report
         * !enabledForView(*view) are skipped: their reads/writes don't
         * participate in lifetime extents, so the graph reflects what
         * will actually run this frame. Idempotent; auto-invoked by
         * execute() when the pass set or enable state changes.
         */
        void compile(const RenderView* view = nullptr);

        /**
         * @brief Temporarily override the pool + backbuffer for the
         *        next execute().
         *
         * Callers that need to drive the same graph against their own
         * offscreen target (editor preview, capture path, ...) push a
         * pre-sized FrameResources + RenderTarget here, run execute(),
         * then pop. While pushed, execute() routes RGResource::Backbuffer
         * to @p target and uses @p pool instead of the lazy default.
         *
         * Pushes do not nest - one outstanding push at a time.
         */
        void pushFrameResources(FrameResources& pool, RenderTarget& target);
        void popFrameResources();

        /**
         * @brief Invalidate the active FrameResources' temporal history
         *        (TAA et al.). Call after any view discontinuity so the
         *        next frame re-primes instead of reprojecting stale data.
         */
        void invalidateTemporalHistory();

    public:
        size_t passCount() const { return m_passes.size(); }
        RenderPass& getPass(size_t index);
        const RenderPass& getPass(size_t index) const;

        const RGResourceLifetime& lifetime(RGResource r) const {
            return m_lifetimes[static_cast<uint32_t>(r)];
        }
        const std::vector<RGResource>& passReads(size_t index)  const { return m_reads[index]; }
        const std::vector<RGResource>& passWrites(size_t index) const { return m_writes[index]; }

        /**
         * @brief Alias-group id for a resource (0..N-1).
         *
         * Resources sharing a group have disjoint lifetimes AND matching
         * descriptors (shape, format, samples - see rgResourceDescriptor),
         * so any member of a group could legally point at the same physical
         * GL object. Computed by compile(); -1 means the resource was never
         * used this frame. Persistent resources get a unique group of their
         * own and never alias. Surfaced in the editor's render-graph
         * visualizer; physical pool sharing that consumes the analysis is
         * deferred (see the class-level docstring).
         */
        int aliasGroup(RGResource r) const {
            return m_aliasGroups[static_cast<uint32_t>(r)];
        }
        std::size_t aliasGroupCount() const { return m_aliasGroupCount; }

        /**
         * @brief Register the concrete backend object backing a logical resource id.
         *
         * The backend calls this from
         * `RenderBackend::populateGraphResources(*this)` at the top of every
         * execute() so the active set is up-to-date; in particular the editor's
         * preview path swaps in a private FrameResources and needs the pool to
         * repoint without recompiling the graph.
         *
         * Storage is type-erased; passes downcast via the typed accessor
         * `RenderGraphContext::resource<T>(id)`.
         */
        void registerResource(RGResource id, void* ptr) {
            m_resources[static_cast<uint32_t>(id)] = ptr;
        }

        /// Raw resource pointer. Prefer `RenderGraphContext::resource<T>()`
        /// at pass call sites; this is the low-level accessor.
        void* getResource(RGResource id) const {
            return m_resources[static_cast<uint32_t>(id)];
        }

    private:
        /// Lazily build m_frame on first need. Picks the active backend.
        FrameResources& ensureFrame(RenderBackend& backend);
        /// Active pool: the preview one when a session is open, else the default.
        FrameResources& activeFrame() const;

#ifndef NDEBUG
        /**
         * @brief Debug-only access-drift check.
         *
         * After every pass.execute() the graph compares
         * ctx.accessedResources against the pass's declared reads/writes
         * and warns about either direction of drift, once per (pass index,
         * resource) pair. The sets m_accessWarn* keep the log from
         * flooding when a real mismatch repeats every frame.
         */
        void checkPassAccess(size_t passIndex, uint32_t accessedMask);
#endif

    private:
        std::vector<std::unique_ptr<RenderPass>> m_passes;
        std::vector<std::vector<RGResource>>     m_reads;
        std::vector<std::vector<RGResource>>     m_writes;
        RGResourceLifetime                       m_lifetimes[RG_RESOURCE_COUNT];
        int                                      m_aliasGroups[RG_RESOURCE_COUNT] = {};
        std::size_t                              m_aliasGroupCount = 0;
        void*                                    m_resources[RG_RESOURCE_COUNT] = {};
        bool                                     m_compiled = false;        ///< compile() has run against the current pass set
        bool                                     m_persistentRegistered = false;

        /**
         * @brief Per-pass enable state captured at the last compile.
         *
         * execute() recomputes the current enable vector each frame and
         * triggers a recompile only when it differs, so toggling
         * env.taa.enabled flips the lifetime data without paying the compile
         * cost every frame.
         */
        std::vector<bool>                        m_lastEnabled;
        uint64_t                                 m_frameIndex = 0;

#ifndef NDEBUG
        std::unordered_set<uint64_t> m_accessWarnDeclaredUnused;  ///< pass-and-resource pairs already warned about
        std::unordered_set<uint64_t> m_accessWarnUndeclared;
#endif

        /// Transient pool for the default viewport. Allocated lazily via
        /// RenderBackend::createFrameResources on first onResize/execute.
        std::unique_ptr<FrameResources>    m_frame;
        uint32_t                           m_width  = 0;
        uint32_t                           m_height = 0;

        /// Currently pushed pool + target, or null when execute() should
        /// use the default. Set by pushFrameResources, cleared by pop.
        FrameResources*                    m_pushedFrame  = nullptr;
        RenderTarget*                      m_pushedTarget = nullptr;
};

} // namespace Engine
