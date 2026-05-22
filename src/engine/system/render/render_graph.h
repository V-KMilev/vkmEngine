#pragma once

#include <memory>
#include <unordered_map>
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
 *   - The transient resource pool (FrameResources). The active pool gets
 *     re-registered every execute() so the editor's offscreen preview
 *     path (which uses a private pool sized for the preview) is just a
 *     temporary swap, not a graph rebuild.
 *   - The editor's offscreen preview lifecycle: target + pool + the
 *     per-key thumbnail cache. RenderSystem orchestrates the editor flow
 *     through graph.beginPreview/endPreview rather than through backend
 *     methods, so the backend interface stays narrow (factories + low-
 *     level primitives).
 *
 * Doesn't own:
 *   - The concrete GL objects inside the pool (GLFrameResources allocates
 *     them; the graph just holds the abstract handle and forwards
 *     resize / registerWith / resolveSceneColor).
 *   - The GLView and the window backbuffer (still backend-owned, since
 *     they're persistent across previews).
 *
 * Future work (tracked in docs/misc/render_roadmap.md, kept local):
 *   - Lifetime-aliasing: resources with disjoint [firstWrite..lastRead]
 *     ranges can share physical storage. The lifetime data is already
 *     computed in compile(); aliasing is a pool reorganisation pass on
 *     top of FrameResources that the graph drives.
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
         * @brief Open an offscreen material preview session at (size, size).
         *
         * Lazily allocates a private FrameResources sized for the preview
         * (separate from the default viewport pool) plus an offscreen
         * RenderTarget that RGResource::Backbuffer routes to while the
         * preview is active. Editor-facing: RenderSystem orchestrates the
         * Material Editor + Asset Browser flow through here.
         *
         * Paired with @ref endPreview(). Idempotent across re-opens at
         * the same size (preserves the pool / target).
         */
        void beginPreview(RenderBackend& backend, uint32_t size);

        /** @brief Close the preview session. The default pool becomes active again. */
        void endPreview();

        /// True while a preview session is open (between beginPreview / endPreview).
        bool isPreviewActive() const { return m_previewActive; }

        /**
         * @brief Backend-typed texture id of the active preview's composited
         *        color output, usable as an ImGui ImTextureID. 0 outside a
         *        preview session.
         */
        uint32_t previewColorTexture() const;

        /**
         * @brief Copy the just-rendered preview into a stable per-key
         *        thumbnail texture and return its backend-typed id.
         *
         * The single preview target is overwritten by the next render, so
         * the Asset Browser grid and the live Material Editor each call
         * here to own a persistent copy keyed by their asset.
         */
        uint32_t snapshotPreviewToCache(RenderBackend& backend,
                                        uint64_t key, uint32_t size);

        /** @brief Cached thumbnail id for @p key, or 0 if never snapshotted. */
        uint32_t cachedPreview(RenderBackend& backend, uint64_t key) const;

        /**
         * @brief Drop a single thumbnail from both the graph-side id map
         *        and the backend's underlying texture cache.
         *
         * Call this when the source asset has been destroyed; otherwise
         * long editing sessions leak textures for assets the user has
         * already removed.
         *
         * @param backend The render backend whose cache also holds @p key.
         * @param key Thumbnail cache key (asset-derived; see asset_browser).
         */
        void evictThumbnail(RenderBackend& backend, uint64_t key);

        /** @brief Drop every cached thumbnail (graph + backend). */
        void clearThumbnailCache(RenderBackend& backend);

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

        /// Preview-session state. m_previewFrame matches m_previewSize and
        /// is held across endPreview so a repeat beginPreview at the same
        /// size is allocation-free.
        std::unique_ptr<FrameResources>    m_previewFrame;
        std::unique_ptr<RenderTarget>      m_previewTarget;
        uint32_t                           m_previewSize   = 0;
        bool                               m_previewActive = false;

        /// Per-key thumbnail snapshot cache. Held by id (backends own the
        /// concrete texture; we only remember which ids we've handed out).
        std::unordered_map<uint64_t, uint32_t> m_thumbCache;
};

} // namespace Engine
