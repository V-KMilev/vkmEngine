#pragma once

#include <deque>
#include <memory>
#include <string>

#include "core/system.h"
#include "core/frame/arrival_hold.h"
#include "system/render/render_backend.h"
#include "io/project/render_settings.h"
#include "system/render/render_view.h"
#include "system/render/screenshot.h"

namespace Vkm::Engine {

/**
 * @brief The engine's entry point into rendering.
 *
 * Runs after Visibility, whose product it draws. Builds the RenderView each
 * frame and hands it to the backend it owns; nothing here is API-specific.
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
         * A no-op with no backend. Starts reading back the frame a Graphics::saveScreenshot asked
         * for, and has each read that has landed written on a worker.
         *
         * @param ctx The frame: its scene, Visibility product and window.
         */
        void update(FrameContext& ctx) override;

        /// Write every screenshot still being read or written, waiting for each: a closing host's.
        void shutdown() override;

        /**
         * @brief Bring a backend up against the window and draw through it.
         *
         * Called once, at startup, with the window's API context current. Throws
         * std::runtime_error when init fails, so the host exits rather than run a black window.
         *
         * @param backend The backend to install.
         * @param window  The window it draws into.
         */
        void setBackend(std::unique_ptr<RenderBackend> backend, Platform::WindowManager& window);

        /**
         * @brief Free each texture's CPU pixels once the backend holds them.
         *
         * For a host that never reads pixels after upload (a cooking host keeps
         * them). After each frame, textures the backend holdsPixels for are
         * released; params stay.
         *
         * @param release Whether to release them.
         */
        void releaseUploadedPixels(bool release) { m_releaseUploadedPixels = release; }

        /**
         * @brief Identity of the active backend, for status displays.
         *
         * @return Empty strings until one is installed.
         */
        BackendInfo backendInfo() const { return m_backend ? m_backend->info() : BackendInfo{}; }

        /**
         * @brief The active backend's anisotropic-filtering ceiling.
         *
         * @return Maximum degree; 1 with no backend or no support.
         */
        uint32_t maxAnisotropy() const { return m_backend ? m_backend->maxAnisotropy() : 1; }

        /**
         * @brief The active backend, or nullptr before setBackend().
         *
         * @return The backend (non-owning), or nullptr.
         */
        RenderBackend* backend() const { return m_backend.get(); }

    private:
        /**
         * @brief Start reading back the frame the backend just drew, for @p path once it lands.
         *
         * @param path From the game's request: a `.png`, or a `.rgb` for its bare rows.
         */
        void readScreenshot(std::string path);

        /**
         * @brief Hand each landed read to the writer, oldest first.
         *
         * @param wait Wait for the reads still on their way, rather than leave them for a later frame.
         */
        void writeLandedScreenshots(bool wait);

        /**
         * @brief Release the pixels of every texture the backend now holds.
         *
         * @param resources The graph walked.
         */
        void releaseHeldPixels(ResourceManager& resources) const;

    private:
        std::unique_ptr<RenderBackend> m_backend;

        RenderView m_view;

        std::deque<std::string> m_reading;  ///< Each screenshot's path, in the order its frame was read.
        ScreenshotWriter        m_screenshots;
        ArrivalHold             m_bakeHold;  ///< An arriving world's assets and sky, which its first bakes
                                             ///< wait for.

        bool m_releaseUploadedPixels = false;
};

} // namespace Vkm::Engine
