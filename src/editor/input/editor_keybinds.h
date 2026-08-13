#pragma once

#include <imgui.h>
#include <cstdint>

namespace Engine {

/**
 * @brief Modifier flags (bitmask) for keybind combos.
 *
 * OR the values together to require several modifiers at once for one keybind.
 */
enum KeyMod : uint8_t {
    KeyMod_None  = 0,
    KeyMod_Ctrl  = 1 << 0,
    KeyMod_Shift = 1 << 1,
    KeyMod_Alt   = 1 << 2,
};

/**
 * @brief A single keybind: one ImGuiKey plus a modifier bitmask.
 */
struct KeyBind {
    ImGuiKey key  = ImGuiKey_None;
    uint8_t  mods = KeyMod_None;

    bool operator==(const KeyBind& o) const { return key == o.key && mods == o.mods; }
    bool operator!=(const KeyBind& o) const { return !(*this == o); }
};

/**
 * @brief All configurable editor keybinds with industry-standard defaults.
 *
 * Persisted via EditorSettings so user rebindings survive across sessions.
 */
struct EditorKeybinds {
    // File
    KeyBind newScene         = { ImGuiKey_N,      KeyMod_Ctrl };
    KeyBind saveScene        = { ImGuiKey_S,      KeyMod_Ctrl };
    KeyBind saveSceneAs      = { ImGuiKey_S,      KeyMod_Ctrl | KeyMod_Shift };
    KeyBind loadScene        = { ImGuiKey_O,      KeyMod_Ctrl };

    // Edit
    KeyBind undo             = { ImGuiKey_Z,      KeyMod_Ctrl };
    KeyBind redo             = { ImGuiKey_Z,      KeyMod_Ctrl | KeyMod_Shift };

    // Panel toggles
    KeyBind toggleHierarchy  = { ImGuiKey_1,      KeyMod_Ctrl };
    KeyBind toggleInspector  = { ImGuiKey_2,      KeyMod_Ctrl };
    KeyBind toggleBottom     = { ImGuiKey_3,      KeyMod_Ctrl };
    KeyBind toggleEditor     = { ImGuiKey_F5,     KeyMod_None };
    KeyBind toggleRenderSettings = { ImGuiKey_4,  KeyMod_Ctrl };
    KeyBind toggleMaterialEditor = { ImGuiKey_5,  KeyMod_Ctrl };
    KeyBind toggleAssetBrowser   = { ImGuiKey_6,  KeyMod_Ctrl };
    KeyBind openPreferences  = { ImGuiKey_Comma,  KeyMod_Ctrl };

    // Entity operations
    KeyBind deleteEntity     = { ImGuiKey_Delete, KeyMod_None };
    KeyBind deselect         = { ImGuiKey_Escape, KeyMod_None };
    KeyBind duplicate        = { ImGuiKey_D,      KeyMod_Ctrl };
    KeyBind focusSelected    = { ImGuiKey_F,      KeyMod_None };
    KeyBind frameAll         = { ImGuiKey_F,      KeyMod_Shift };

    // Gizmo modes (only active when camera NOT in fly mode)
    KeyBind gizmoSelect      = { ImGuiKey_Q, KeyMod_None };
    KeyBind gizmoTranslate   = { ImGuiKey_W, KeyMod_None };
    KeyBind gizmoRotate      = { ImGuiKey_E, KeyMod_None };
    KeyBind gizmoScale       = { ImGuiKey_R, KeyMod_None };
    KeyBind gizmoToggleSpace = { ImGuiKey_X, KeyMod_None };
};

/**
 * @brief Check whether a keybind was just pressed this frame.
 *
 * Requires an exact modifier match: a Ctrl-only bind does not fire while Shift
 * is also held, so overlapping combos (e.g. Undo vs Redo) stay distinct.
 *
 * @param bind Keybind to test; an unbound (ImGuiKey_None) bind never matches.
 * @return true on the frame the key transitions to pressed with matching modifiers.
 */
inline bool isPressed(const KeyBind& bind) {
    if (bind.key == ImGuiKey_None) return false;
    if (!ImGui::IsKeyPressed(bind.key)) return false;

    const ImGuiIO& io = ImGui::GetIO();
    if (((bind.mods & KeyMod_Ctrl)  != 0) != io.KeyCtrl)  return false;
    if (((bind.mods & KeyMod_Shift) != 0) != io.KeyShift) return false;
    if (((bind.mods & KeyMod_Alt)   != 0) != io.KeyAlt)   return false;

    return true;
}

/**
 * @brief Format a human-readable label for a keybind (e.g. "Ctrl+D", "F5", "W").
 *
 * @param bind Keybind whose modifiers and key are rendered into text.
 * @param buf Caller-provided buffer the label is written into.
 * @param bufSize Capacity of @p buf in bytes; the label is truncated to fit.
 * @return @p buf, for convenient inline use in menu/tooltip strings.
 */
const char* getKeyBindLabel(const KeyBind& bind, char* buf, size_t bufSize);

/**
 * @brief Value-returning keybind label, for inline use as a MenuItem shortcut:
 * `ImGui::MenuItem("Save Scene", keyLabel(kb.saveScene))`.
 *
 * Wraps a small fixed buffer that implicitly decays to const char*, so call
 * sites don't juggle a scratch char[] + size. The temporary lives to the end of
 * the full expression - long enough for ImGui, which renders the shortcut
 * during the call rather than storing the pointer.
 */
struct KeyLabel {
    char buf[48];
    operator const char*() const { return buf; }
};
inline KeyLabel keyLabel(const KeyBind& bind) {
    KeyLabel out;
    getKeyBindLabel(bind, out.buf, sizeof(out.buf));
    return out;
}

} // namespace Engine
