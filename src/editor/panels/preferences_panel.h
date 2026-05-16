#pragma once

#include "framework/editor_panel.h"

namespace Engine {

struct FrameContext;
struct EditorState;
struct EditorContext;

/**
 * @brief Editor/application Preferences window.
 *
 * A floating, closeable window (opened from Edit > Preferences, Ctrl+,)
 * with a grouped master-detail layout. Holds user/app configuration as
 * opposed to per-scene data: fly-camera tuning, gizmo snap defaults,
 * window/display options, and keybind rebinding.
 */
class PreferencesPanel : public EditorPanel {
    public:
        const char* panelId() const override { return "Preferences"; }

        /// Draws the window while state.showPreferences is true; the
        /// title-bar X clears it.
        void draw(EditorContext& ec) override;

    private:
        void drawCameraSection(EditorContext& ec);
        void drawGizmoSection(EditorState& state);
        void drawDisplaySection(FrameContext& ctx);
        void drawKeybindsSection(EditorState& state);

        int m_selectedSection = 0;
};

} // namespace Engine
