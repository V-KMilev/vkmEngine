#pragma once

#include <memory>

#include "core/system.h"
#include "system/render/render_backend.h"
#include "system/render/render_view.h"

namespace Engine {

/**
 * @brief The engine's entry point into rendering.
 *
 * Runs in the Render stage. Each frame it snapshots the visible scene into a
 * RenderView (reused across frames for its capacity) and hands that to the
 * active backend, which does the actual drawing. RenderSystem owns the backend
 * and the snapshot - nothing graphics-API-specific lives here.
 */
class RenderSystem : public System {
    public:
        RenderSystem() = default;
        ~RenderSystem() override = default;

        RenderSystem(const RenderSystem& other) = delete;
        RenderSystem& operator=(const RenderSystem& other) = delete;

        RenderSystem(RenderSystem && other) = delete;
        RenderSystem& operator=(RenderSystem && other) = delete;

    public:
        /**
         * @brief Run one frame of rendering.
         *
         * Applies any pending backend swap, snapshots the visible scene into the
         * RenderView (reused across frames for its capacity), and hands that to
         * the active backend to draw. A no-op until a backend is installed.
         */
        void update(FrameContext& ctx) override;

        /**
         * @brief Install a backend, hot-swappable at runtime.
         *
         * The swap is applied at the start of the next update(): the incoming
         * backend is brought up (init) against the window first, and only on
         * success does it replace - and destroy - the current one. A backend
         * that fails to init is dropped and the current one keeps running, so a
         * bad swap never leaves a black screen.
         */
        void setBackend(std::unique_ptr<RenderBackend> backend);

        /**
         * @brief Identity of the active backend for the editor's status displays.
         *
         * Empty strings until a backend is installed.
         */
        BackendInfo backendInfo() const { return m_backend ? m_backend->info() : BackendInfo{}; }

        /**
         * @brief The active backend, or nullptr before the first install.
         *
         * Non-owning; editor-side consumers (screenshot capture) only.
         */
        RenderBackend* backend() const { return m_backend.get(); }

    private:
        /**
         * @brief Apply a queued backend swap, if one is pending.
         *
         * Inits the queued backend against the window and, on success, makes it
         * current (destroying the previous one). Runs at the top of update() so
         * a swap from setup or the editor lands on the next frame.
         */
        void installPending(FrameContext& ctx);

    private:
        std::unique_ptr<RenderBackend> m_backend;
        std::unique_ptr<RenderBackend> m_pending;

        RenderView m_view;
};

} // namespace Engine
