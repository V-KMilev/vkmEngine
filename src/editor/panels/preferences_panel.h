#pragma once

namespace Engine {

class CameraController;
struct FrameContext;
struct EditorState;

/**
 * @brief Editor/application Preferences window.
 *
 * A floating, closeable window (opened from Edit > Preferences, Ctrl+,)
 * with a grouped master-detail layout. Holds the settings that are user/app
 * configuration rather than per-scene data: fly-camera tuning, gizmo snap
 * defaults, window/display options, and keybind rebinding. These were
 * previously crammed into the bottom panel; they live here so the bottom
 * panel can stay focused on the scene/working surface.
 */
class PreferencesPanel {
    public:
        explicit PreferencesPanel(CameraController* cam) : m_cameraController(cam) {}

        /// Draws the window when *open is true; the title-bar X writes false.
        void draw(bool* open, FrameContext& ctx, EditorState& state);

    private:
        void drawCameraSection();
        void drawGizmoSection(EditorState& state);
        void drawDisplaySection(FrameContext& ctx);
        void drawKeybindsSection(EditorState& state);

        int m_selectedSection = 0;
        CameraController* m_cameraController = nullptr;
};

} // namespace Engine
