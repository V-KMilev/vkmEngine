#pragma once

namespace Engine {

struct FrameContext;
struct EditorState;
struct EditorContext;

/**
 * @brief Editor/application Preferences window.
 *
 * A floating, closeable window (opened from Edit > Preferences, Ctrl+,) with a
 * tabbed layout (Camera / Gizmo / Display / Keybinds). Holds user/app
 * configuration as opposed to per-scene data.
 */
class PreferencesPanel {
    public:
        PreferencesPanel() = default;
        ~PreferencesPanel() = default;

        PreferencesPanel(const PreferencesPanel& other) = delete;
        PreferencesPanel& operator=(const PreferencesPanel& other) = delete;

        PreferencesPanel(PreferencesPanel && other) = delete;
        PreferencesPanel& operator=(PreferencesPanel && other) = delete;

    public:
        /**
         * @brief Draws the window while state.showPreferences is true; the
         * title-bar X clears it.
         */
        void draw(EditorContext& ec);

    private:
        void drawCameraSection(EditorContext& ec);
        void drawGizmoSection(EditorState& state);
        void drawDisplaySection(FrameContext& ctx);
        void drawKeybindsSection(EditorState& state);

        // Display section: FPS cap value the InputInt edits before "Apply".
        int m_fpsLimitEdit = 0;

        // Keybinds section: identifies which row is currently capturing a
        // keystroke. Stable across calls; nullptr means no active rebind.
        // Uses label pointer-identity, matching the rebind row labels below.
        const char* m_rebindTarget = nullptr;
};

} // namespace Engine
