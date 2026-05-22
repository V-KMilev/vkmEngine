#pragma once

#include <imgui.h>
#include <cstdint>

namespace Engine {

/// Modifier flags (bitmask) for keybind combos.
enum KeyMod : uint8_t {
    KeyMod_None  = 0,
    KeyMod_Ctrl  = 1 << 0,
    KeyMod_Shift = 1 << 1,
    KeyMod_Alt   = 1 << 2,
};

/// A single keybind: one ImGuiKey + modifier bitmask.
struct KeyBind {
    ImGuiKey key  = ImGuiKey_None;
    uint8_t  mods = KeyMod_None;

    bool operator==(const KeyBind& o) const { return key == o.key && mods == o.mods; }
    bool operator!=(const KeyBind& o) const { return !(*this == o); }
};

/// All configurable editor keybinds with industry-standard defaults.
struct EditorKeybinds {
    // File
    KeyBind saveScene        = { ImGuiKey_S,      KeyMod_Ctrl };
    KeyBind saveSceneAs      = { ImGuiKey_S,      KeyMod_Ctrl | KeyMod_Shift };
    KeyBind loadScene        = { ImGuiKey_O,      KeyMod_Ctrl };

    // Edit
    KeyBind undo             = { ImGuiKey_Z,      KeyMod_Ctrl };
    KeyBind redo             = { ImGuiKey_Z,      KeyMod_Ctrl | KeyMod_Shift };

    // Panel toggles
    KeyBind toggleStats      = { ImGuiKey_F1,     KeyMod_None };
    KeyBind toggleHierarchy  = { ImGuiKey_1,      KeyMod_Ctrl };
    KeyBind toggleInspector  = { ImGuiKey_2,      KeyMod_Ctrl };
    KeyBind toggleBottom     = { ImGuiKey_3,      KeyMod_Ctrl };
    KeyBind toggleEditor     = { ImGuiKey_F5,     KeyMod_None };
    KeyBind openPreferences  = { ImGuiKey_Comma,  KeyMod_Ctrl };

    // Entity operations
    KeyBind deleteEntity     = { ImGuiKey_Delete, KeyMod_None };
    KeyBind deselect         = { ImGuiKey_Escape, KeyMod_None };
    KeyBind duplicate        = { ImGuiKey_D,      KeyMod_Ctrl };
    KeyBind focusSelected    = { ImGuiKey_F,      KeyMod_None };

    // Gizmo modes (only active when camera NOT in fly mode)
    KeyBind gizmoSelect      = { ImGuiKey_Q, KeyMod_None };
    KeyBind gizmoTranslate   = { ImGuiKey_W, KeyMod_None };
    KeyBind gizmoRotate      = { ImGuiKey_E, KeyMod_None };
    KeyBind gizmoScale       = { ImGuiKey_R, KeyMod_None };
    KeyBind gizmoToggleSpace = { ImGuiKey_X, KeyMod_None };
};

/// Check if a keybind was just pressed this frame (exact modifier match).
inline bool isPressed(const KeyBind& bind) {
    if (bind.key == ImGuiKey_None) return false;
    if (!ImGui::IsKeyPressed(bind.key)) return false;

    const ImGuiIO& io = ImGui::GetIO();
    if (((bind.mods & KeyMod_Ctrl)  != 0) != io.KeyCtrl)  return false;
    if (((bind.mods & KeyMod_Shift) != 0) != io.KeyShift) return false;
    if (((bind.mods & KeyMod_Alt)   != 0) != io.KeyAlt)   return false;

    return true;
}

/// Get a human-readable label for a keybind (e.g. "Ctrl+D", "F5", "W").
const char* getKeyBindLabel(const KeyBind& bind, char* buf, size_t bufSize);

} // namespace Engine
