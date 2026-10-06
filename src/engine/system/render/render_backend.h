#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "resource/resource_handle.h"

namespace Vkm::Engine {

struct RenderView;
struct TextureAsset;
class ResourceManager;
class WindowManager;
class EditorRenderHooks;

/**
 * @brief Human-readable backend identity, for a status display.
 *
 * Generic strings: another backend changes them, never the call site.
 */
struct BackendInfo {
    std::string api;     ///< e.g. "OpenGL 4.6"
    std::string device;  ///< e.g. "NVIDIA GeForce RTX 3080"
};

/**
 * @brief The frame's seam between the engine and the graphics API.
 *
 * The backend gets one POD RenderView per frame and owns all GPU state below
 * this line; the window owns the API context and Engine::run presents. Nothing
 * above holds an API object.
 */
class RenderBackend {
    public:
        RenderBackend() = default;
        virtual ~RenderBackend() = default;

        RenderBackend(const RenderBackend& other) = delete;
        RenderBackend& operator=(const RenderBackend& other) = delete;

        RenderBackend(RenderBackend && other) = delete;
        RenderBackend& operator=(RenderBackend && other) = delete;

    public:
        /**
         * @brief Bring the backend up against the window.
         *
         * The window's API context is current. False when the device cannot run
         * this backend.
         *
         * @param window The window whose context the backend draws into.
         * @return True when the backend can draw.
         */
        virtual bool init(WindowManager& window) = 0;

        /**
         * @brief Draw one frame into the window's back buffer, for the engine loop to present.
         *
         * The view carries the viewport and surface size; a backend detects a
         * resize by comparing against its own cached values.
         *
         * @param view      What to draw this frame.
         * @param resources Resolves the view's handles; only changed data is uploaded.
         */
        virtual void render(const RenderView& view, const ResourceManager& resources) = 0;

        /**
         * @brief Read back the frame render() last drew, as the window shows it.
         *
         * The viewport after every pass, UI included. Called right after render()
         * in the same frame, so a backend presenting in render() reads first.
         * Leaves no API state behind.
         *
         * @param view   The view render() was handed; its viewport is the rect read.
         * @param pixels Replaced with tightly packed 8-bit RGB, top row first.
         * @return False, with @p pixels empty, when this backend cannot read back.
         */
        virtual bool readFrame(const RenderView& view, std::vector<uint8_t>& pixels) {
            pixels.clear();
            return false;
        }

        /**
         * @brief Recompile shaders whose source changed on disk - a dev hook.
         *
         * Cheap enough per frame (a directory scan and timestamp compare). A
         * failing edit keeps the old program and logs. A no-op without sources on disk.
         *
         * @return Number of shaders recompiled.
         */
        virtual uint32_t reloadChangedShaders() { return 0; }

        /**
         * @brief The highest anisotropic-filtering degree this backend can honour.
         *
         * What the driver reports; a setting should offer nothing above it.
         *
         * @return Maximum degree; 1 means none.
         */
        virtual uint32_t maxAnisotropy() const { return 1; }

        /**
         * @brief Whether the backend holds its own copy of a texture's pixels.
         *
         * Asked before the CPU copy is freed (see RenderSystem::releaseUploadedPixels).
         *
         * @param texture The texture asked about.
         * @param version Its asset's current Resource::version.
         * @return True only when the backend's copy is of that version.
         */
        virtual bool holdsPixels(
            const Handle<TextureAsset>& texture,
            uint64_t version
        ) const { return false; }

        /**
         * @brief The backend's authoring-only hooks, or null when it offers none.
         *
         * @return Hooks the editor may use, or null.
         */
        virtual EditorRenderHooks* editorHooks() { return nullptr; }

    public:
        BackendInfo info() const { return m_info; }

    protected:
        /// What info() reports, once init() knows the API and the device.
        void setInfo(BackendInfo info) { m_info = std::move(info); }

    private:
        BackendInfo m_info;
};

} // namespace Vkm::Engine
