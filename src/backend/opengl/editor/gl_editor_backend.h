#pragma once

#include "gl_backend.h"

namespace Vkm::Engine {

/**
 * @brief GLBackend with ImGui's OpenGL renderer, which only the editor's host links.
 *
 * ImGui's half of EditorRenderHooks, a target of its own (vkm_backend_gl_editor) apart from the
 * backend's, so that no other host links ImGui.
 */
class GLEditorBackend final : public GLBackend {
    public:
        GLEditorBackend() = default;
        ~GLEditorBackend() override = default;

        GLEditorBackend(const GLEditorBackend& other) = delete;
        GLEditorBackend& operator=(const GLEditorBackend& other) = delete;

        GLEditorBackend(GLEditorBackend && other) = delete;
        GLEditorBackend& operator=(GLEditorBackend && other) = delete;

    public:
        void initImGui(Platform::WindowManager& window) override;
        void beginImGuiFrame() override;
        void renderImGui(ImDrawData* data) override;
        void shutdownImGui() override;
};

} // namespace Vkm::Engine
