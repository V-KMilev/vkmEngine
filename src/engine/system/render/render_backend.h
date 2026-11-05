#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "resource/handle.h"
#include "core/frame/render_readiness.h"

namespace Vkm::Platform {
class WindowManager;
} // namespace Vkm::Platform

namespace Vkm::Engine {

struct RenderView;
struct TextureAsset;
class ResourceManager;
class EditorRenderHooks;

/**
 * @brief Human-readable backend identity, for a status display.
 *
 * Generic strings: another backend changes them, never the call site.
 */
struct BackendInfo {
    std::string api;     ///< The graphics API and its version.
    std::string device;  ///< The GPU, as its driver names it.
};

/**
 * @brief A frame read back from the GPU: 8-bit RGB, top row first, rows packed tight.
 */
struct FramePixels {
    uint32_t             width  = 0;
    uint32_t             height = 0;
    std::vector<uint8_t> rgb;         ///< width * height * 3 bytes.
};

inline constexpr uint32_t BACKEND_MIN_TEXTURE_EDGE = 16384;  ///< The widest texture edge every backend takes,
                                                             ///< in texels: one that cannot refuses to init,
                                                             ///< so a bake may count on it.

/**
 * @brief The frame's seam between the engine and the graphics API.
 *
 * The backend gets one RenderView per frame and owns all GPU state below
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
        virtual bool init(Platform::WindowManager& window) = 0;

        /**
         * @brief Draw one frame into the window's back buffer, for the engine loop to present.
         *
         * The view carries the viewport and surface size; a backend detects a
         * resize by comparing against its own cached values.
         *
         * @param view      What to draw this frame; nothing of it is read once this returns, as
         *                  what it points at is rewritten for the next.
         * @param resources Resolves the view's handles; only changed data is uploaded.
         */
        virtual void render(const RenderView& view, const ResourceManager& resources) = 0;

        /**
         * @brief Start reading back the frame render() last drew, as the window shows it, without
         *        waiting for the GPU to finish drawing it: takeFrame hands it over once it has.
         *
         * The viewport after every pass, UI included. Called right after render()
         * in the same frame, so a backend presenting in render() reads first.
         * Leaves no API state behind.
         *
         * @param view The view render() was handed; its viewport is the rect read.
         * @return False, reading nothing, when the viewport is empty.
         */
        virtual bool readFrame(const RenderView& view) = 0;

        /**
         * @brief Hand over the oldest frame readFrame started, once the GPU has written it.
         *
         * @param frame Replaced with it.
         * @param wait  Wait for it rather than answer false while it is on its way: for a host closing.
         * @return False when no read is waiting, or, without @p wait, the oldest has not landed.
         */
        virtual bool takeFrame(FramePixels& frame, bool wait) = 0;

        /**
         * @brief The highest anisotropic-filtering degree this backend can honor.
         *
         * What the driver reports; a setting should offer nothing above it.
         *
         * @return Maximum degree; 1 means none.
         */
        virtual uint32_t maxAnisotropy() const = 0;

        /**
         * @brief Whether the backend holds its own copy of a texture's pixels.
         *
         * Asked before the CPU copy is freed (see RenderSystem::releaseUploadedPixels).
         *
         * @param texture The texture asked about.
         * @param version Its asset's current Resource::version.
         * @return True only when the backend's copy is of that version.
         */
        virtual bool holdsPixels(const Handle<TextureAsset>& texture, uint64_t version) const = 0;

        /**
         * @brief The backend's authoring-only hooks, or null when it offers none.
         *
         * @return Hooks the editor may use, or null.
         */
        virtual EditorRenderHooks* editorHooks() = 0;

        /**
         * @brief How far the startup programs, a world drawn whole behind the loading screen, and the
         *        bakes the frames so far started or hold have come.
         *
         * @return After the last render(); ready when nothing is owed.
         */
        virtual RenderReadiness readiness() const = 0;

    public:
        BackendInfo info() const { return m_info; }

    protected:
        /// What info() reports, once init() knows the API and the device.
        void setInfo(BackendInfo info) { m_info = std::move(info); }

    private:
        BackendInfo m_info;
};

} // namespace Vkm::Engine
