#pragma once

namespace Vkm::Engine {

struct FrameContext;
struct EditorState;
struct EditorContext;

/**
 * @brief The Preferences window (Edit > Preferences): per-user settings, not scene data.
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
         * @brief Draw the window while EditorState::showPreferences is set; the title-bar X clears it.
         *
         * @param ec Context whose Preferences are edited.
         */
        void draw(EditorContext& ec);

        /**
         * @brief Whether a keybind row is waiting for the key to bind.
         *
         * While armed no shortcut may act: a Delete being bound must not delete the selection.
         *
         * @return True from the click on a row until a key or a click ends it.
         */
        bool isCapturingKey() const { return m_rebindTarget != nullptr; }

    private:
        void drawCameraSection(EditorContext& ec);
        void drawGizmoSection(EditorState& state);
        void drawDisplaySection(EditorContext& ec);
        void drawKeybindsSection(EditorState& state);

    private:
        int m_fpsLimitEdit = 0;  ///< FPS cap being typed, taken on Enter; seeded as the window appears.

        const char* m_rebindTarget = nullptr;  ///< The capturing row, by its KEYBINDS label's address,
                                               ///< which is stable across frames.
};

} // namespace Vkm::Engine
