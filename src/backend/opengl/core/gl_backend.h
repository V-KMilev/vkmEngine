#pragma once

#include <memory>
#include <cstdint>
#include <unordered_map>

#include "system/render/render_backend.h"

#include "gl_context.h"
#include "gl_view.h"
#include "gl_render_target.h"
#include "gl_hdr_target.h"
#include "resource/gl_bloom.h"
#include "resource/gl_auto_exposure.h"
#include "resource/gl_gbuffer.h"
#include "gl_frame_resources.h"

namespace Engine {
    struct RenderView;
    class ResourceManager;
}

namespace Engine {

/**
 * @brief OpenGL implementation of the RenderBackend interface.
 *
 * GLBackend bridges the high-level render system with the OpenGL API.
 * It manages the overall rendering workflow, including viewport resizing,
 * context handling, and mesh-resource management.
 *
 * This class prevents copy and move operations to guarantee a unique association
 * with OpenGL resources, which cannot be safely duplicated or transferred.
 */
class GLBackend : public RenderBackend {
    public:
        GLBackend();
        ~GLBackend() = default;

        GLBackend(const GLBackend& other) = delete;
        GLBackend& operator=(const GLBackend& other) = delete;

        GLBackend(GLBackend && other) = delete;
        GLBackend& operator=(GLBackend && other) = delete;

    public:
        /**
         * @brief Resize the render target or OpenGL viewport.
         *
         * Adjusts the OpenGL viewport and related render targets to match the provided dimensions.
         * Typically called in response to a window or framebuffer resize event.
         *
         * @param width  New width in pixels.
         * @param height New height in pixels.
         */
        void resize(uint32_t width, uint32_t height) override;
        RenderTarget& getDefaultTarget() override {
            return m_previewMode ? static_cast<RenderTarget&>(*m_previewTarget)
                                 : static_cast<RenderTarget&>(m_defaultTarget);
        }
        void setWireframe(bool enabled) override;
        void syncResources(const RenderView& view, const ResourceManager& resources) override;
        void resolveSceneColor() override { frame().hdr().resolve(); }

        /// Offscreen preview path (Material Editor / Asset Browser).
        /// Redirects the whole graph's targets to a private preview set so
        /// the unmodified pipeline can be re-run into an FBO. See RenderBackend.
        void     beginPreview(uint32_t size) override;
        // Leave preview mode AND rebind the real backbuffer + viewport. The
        // preview's composite pass left the offscreen preview FBO bound;
        // without this, ImGui (which renders into the currently bound FBO)
        // would draw the whole editor into the 512x512 preview texture and
        // the screen would show only the bare scene.
        void     endPreview() override {
            m_previewMode = false;
            m_defaultTarget.bind();
        }
        uint32_t previewColorTexture() const override;
        uint32_t snapshotPreviewToCache(uint64_t key, uint32_t size) override;
        uint32_t cachedPreview(uint64_t key) const override;

        /**
         * @brief Get the OpenGL rendering context.
         *
         * Provides access to the low-level OpenGL context for state management and rendering operations.
         * Use this for advanced OpenGL operations not exposed by the high-level API.
         *
         * @return Reference to the OpenGL context.
         */
        Core::Context& getContext() { return m_context; }

        /**
         * @brief Get the OpenGL rendering context (const version).
         *
         * Provides read-only access to the OpenGL context for querying state.
         *
         * @return Const reference to the OpenGL context.
         */
        const Core::Context& getContext() const { return m_context; }

        /**
         * @brief Get the OpenGL view for resource management.
         *
         * The GLView manages all GPU-side resources (meshes, materials, textures, lights)
         * and handles synchronization with the CPU-side resource manager.
         *
         * This is the main interface for:
         * - Syncing resources to GPU
         * - Accessing GPU resources during rendering
         * - Managing resource lifecycle and cleanup
         *
         * @return Reference to the internal GLView.
         */
        GLView& getView() { return m_view; }

        /**
         * @brief Get the OpenGL view (const version).
         *
         * Provides read-only access to the GLView for querying GPU resources.
         *
         * @return Const reference to the internal GLView.
         */
        const GLView& getView() const { return m_view; }

        /**
         * @brief Offscreen linear-HDR scene target (MSAA RGBA16F + resolve).
         *
         * The scene passes render into this; the composite pass resolves it
         * and applies exposure + AgX tone mapping to the backbuffer. Lighting
         * is therefore never clamped before tone mapping.
         */
        GLHdrTarget&       getHdrTarget()       { return frame().hdr(); }
        const GLHdrTarget& getHdrTarget() const { return frame().hdr(); }

        /// Bloom mip-chain target (built from the resolved HDR scene).
        GLBloom&       getBloom()       { return frame().bloom(); }
        const GLBloom& getBloom() const { return frame().bloom(); }

        /// Auto-exposure metering + adaptation targets.
        GLAutoExposure&       getAutoExposure()       { return frame().autoExposure(); }
        const GLAutoExposure& getAutoExposure() const { return frame().autoExposure(); }

        /// View-space normal/position G-buffer + AO target (GTAO).
        GLGBuffer&       getGBuffer()       { return frame().gbuffer(); }
        const GLGBuffer& getGBuffer() const { return frame().gbuffer(); }

        /// TAA ping-pong history target.
        GLTAA&       getTAA()       { return frame().taa(); }
        const GLTAA& getTAA() const { return frame().taa(); }

        /// Shared scratch target for in-place post passes (DoF, motion blur).
        GLPostScratch&       getPostScratch()       { return frame().scratch(); }
        const GLPostScratch& getPostScratch() const { return frame().scratch(); }

        /// The render graph's transient resource pool (HDR/bloom/AO/exposure).
        FrameResources&       getFrameResources()       { return frame(); }
        const FrameResources& getFrameResources() const { return frame(); }

    private:
        /// The active transient pool: the private preview set while a preview
        /// is rendering, the window-sized set otherwise. This is what lets the
        /// whole render graph run into an offscreen target untouched.
        FrameResources&       frame()       { return m_previewMode ? m_previewFrame : m_frame; }
        const FrameResources& frame() const { return m_previewMode ? m_previewFrame : m_frame; }

    private:
        Core::Context m_context;
        GLView m_view;
        GLDefaultRenderTarget m_defaultTarget;
        FrameResources m_frame;

        // Preview render-target context (offscreen, editor-driven).
        FrameResources                       m_previewFrame;
        std::unique_ptr<GLFramebufferTarget> m_previewTarget;
        bool                                 m_previewMode = false;
        uint32_t                             m_previewSize = 0;

        // Persistent per-key thumbnail snapshots (Asset Browser grid + the
        // live Material Editor). Copied off the shared preview target so it
        // can be overwritten by the next render without aliasing.
        std::unordered_map<uint64_t, std::unique_ptr<Core::Texture2D>> m_thumbCache;
};

} // namespace Engine
