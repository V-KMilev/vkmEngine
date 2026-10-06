#pragma once

#include <memory>
#include <string>

#include "core/system.h"
#include "system/render/render_backend.h"
#include "system/render/render_settings.h"
#include "system/render/render_view.h"

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
         * A no-op with no backend. Writes any screenshot requested via
         * WindowManager::saveScreenshot.
         *
         * @param ctx The frame: its scene, Visibility product and window.
         */
        void update(FrameContext& ctx) override;

        /**
         * @brief Bring a backend up against the window and draw through it.
         *
         * Called once, at startup, with the window's API context current. Throws
         * std::runtime_error when init fails, so the host exits rather than run a black window.
         *
         * @param backend The backend to install.
         * @param window  The window it draws into.
         */
        void setBackend(std::unique_ptr<RenderBackend> backend, WindowManager& window);

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
         * @brief Write the frame the backend just drew to @p path as a PNG.
         *
         * @param path From the window's request.
         */
        void writeScreenshot(const std::string& path);

        /**
         * @brief Release the pixels of every texture the backend now holds.
         *
         * @param resources The graph walked.
         */
        void releaseHeldPixels(ResourceManager& resources) const;

    private:
        std::unique_ptr<RenderBackend> m_backend;

        RenderView m_view;

        bool m_releaseUploadedPixels = false;
};

} // namespace Vkm::Engine
