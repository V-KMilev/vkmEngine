#pragma once

namespace Engine {

struct EditorContext;

/**
 * @brief Base class for editor panels driven by the panel registry.
 *
 * A panel is anything EditorSystem can own, identify and draw uniformly:
 * one draw(EditorContext&) per frame plus a stable string id. The docked
 * panels (Hierarchy, Inspector, Bottom) and the Preferences window derive
 * from this and are held in EditorSystem's registry.
 *
 * In-viewport overlays (transform gizmo, toolbar, playback bar, stats) are
 * a separate category: they have strict draw ordering and query methods
 * (isHovered / isGizmoOver) that a uniform registry loop cannot express,
 * so they stay concrete members on EditorSystem. They still take an
 * EditorContext& so the per-frame data flows through one type everywhere.
 */
class EditorPanel {
    public:
        virtual ~EditorPanel() = default;

        EditorPanel(const EditorPanel& other) = delete;
        EditorPanel& operator=(const EditorPanel& other) = delete;

        EditorPanel(EditorPanel && other) = delete;
        EditorPanel& operator=(EditorPanel && other) = delete;

        /// Stable identifier; also used as the ImGui id base.
        virtual const char* panelId() const = 0;

        /// Render this panel for the current frame.
        virtual void draw(EditorContext& ctx) = 0;

    protected:
        EditorPanel() = default;
};

} // namespace Engine
