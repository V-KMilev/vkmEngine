#pragma once

#include <cstdint>
#include <string>

namespace Engine {
    struct RenderView;
    class ResourceManager;
    class WindowManager;
}

namespace Engine {

/**
 * @brief Which graphics API a backend speaks.
 * 
 * Drives the runtime "switch backend" menu and is reported back through info().
 */
enum class RenderBackendType {
    OpenGL,
    Optix,
    CPU,
};

/**
 * @brief Human-readable backend identity for the editor status bar.
 * 
 * Deliberately generic: swapping backends changes the strings, never the call site.
 */
struct BackendInfo {
    std::string api;     ///< e.g. "OpenGL 4.6"
    std::string device;  ///< e.g. "NVIDIA GeForce RTX 3080"
};

/**
 * @brief The single seam between the engine and the graphics API.
 *
 * The engine hands the backend one POD RenderView per frame; the backend owns
 * everything below this line - the API context, GPU resource mirror, passes,
 * and presentation. Nothing above this interface holds an API object, so
 * swapping backends is just destroying one implementation and constructing
 * another: there is no GPU state to migrate, and the new backend re-uploads
 * what it needs from the handles in the next RenderView.
 */
class RenderBackend {
    public:
        explicit RenderBackend(RenderBackendType type) : m_type(type) {}
        virtual ~RenderBackend() = default;

        RenderBackend(const RenderBackend& other) = delete;
        RenderBackend& operator=(const RenderBackend& other) = delete;

        RenderBackend(RenderBackend && other) = delete;
        RenderBackend& operator=(RenderBackend && other) = delete;

    public:
        RenderBackendType type() const { return m_type; }
        BackendInfo info() const { return m_info; }

        /**
         * @brief Bring the backend up against the window.
         *
         * Acquires / makes-current the API context or surface and creates any
         * persistent GPU state. Returns false if the backend cannot run here,
         * so a failed runtime swap can keep the previous backend instead.
         */
        virtual bool init(WindowManager& window) = 0;

        /**
         * @brief Set the viewport rect that subsequent frames draw into.
         *
         * x/y are the origin inside the window (lets an editor render into a
         * sub-rect); width/height are its size.
         */
        virtual void resize(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;

        /**
         * @brief Draw and present one frame.
         *
         * @param view      Backend-agnostic snapshot of what to draw this frame.
         * @param resources Resolves the view's handles to asset data; the backend
         *                  mirrors that onto the GPU, uploading only what changed.
         */
        virtual void render(const RenderView& view, const ResourceManager& resources) = 0;

    protected:
        RenderBackendType m_type;
        BackendInfo m_info;
};

} // namespace Engine
