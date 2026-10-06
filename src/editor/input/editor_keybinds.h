#pragma once

#include <cstdint>

#include <imgui.h>

namespace Vkm::Engine {

/**
 * @brief Modifier flags (bitmask) for keybind combos.
 */
enum KeyMod : uint8_t {
    KEY_MOD_NONE  = 0,
    KEY_MOD_CTRL  = 1 << 0,
    KEY_MOD_SHIFT = 1 << 1,
    KEY_MOD_ALT   = 1 << 2,
};

/**
 * @brief A single keybind: one ImGuiKey plus a modifier bitmask.
 */
struct KeyBind {
    ImGuiKey key  = ImGuiKey_None;
    uint8_t  mods = KEY_MOD_NONE;

    bool operator==(const KeyBind& o) const { return key == o.key && mods == o.mods; }
    bool operator!=(const KeyBind& o) const { return !(*this == o); }
};

/**
 * @brief Every configurable keybind, once, as (field, group, label, key, mods).
 *
 * The persisted key is the field's name, so a rename discards saved rebindings.
 * In Preferences display order; rows sharing a group stay contiguous, as
 * PreferencesPanel starts a heading whenever the group changes.
 */
#define VKM_EDITOR_KEYBINDS(X) \
    X(newScene,             "File",                            "New Scene",        ImGuiKey_N,        KEY_MOD_CTRL) \
    X(saveScene,            "File",                            "Save Scene",       ImGuiKey_S,        KEY_MOD_CTRL) \
    X(saveSceneAs,          "File",                            "Save Scene As",    ImGuiKey_S,        KEY_MOD_CTRL | KEY_MOD_SHIFT) \
    X(loadScene,            "File",                            "Load Scene",       ImGuiKey_O,        KEY_MOD_CTRL) \
    X(undo,                 "Edit",                            "Undo",             ImGuiKey_Z,        KEY_MOD_CTRL) \
    X(redo,                 "Edit",                            "Redo",             ImGuiKey_Z,        KEY_MOD_CTRL | KEY_MOD_SHIFT) \
    X(playStop,             "Play (live during a session)",    "Play / Stop",      ImGuiKey_P,        KEY_MOD_CTRL) \
    X(pauseResume,          "Play (live during a session)",    "Pause / Resume",   ImGuiKey_P,        KEY_MOD_CTRL | KEY_MOD_SHIFT) \
    X(ejectView,            "Play (live during a session)",    "Eject / Return",   ImGuiKey_F8,       KEY_MOD_NONE) \
    X(toggleHierarchy,      "Windows & Panels",                "Toggle Hierarchy", ImGuiKey_1,        KEY_MOD_CTRL) \
    X(toggleInspector,      "Windows & Panels",                "Toggle Inspector", ImGuiKey_2,        KEY_MOD_CTRL) \
    X(toggleAssets,         "Windows & Panels",                "Toggle Assets",    ImGuiKey_3,        KEY_MOD_CTRL) \
    X(toggleRenderSettings, "Windows & Panels",                "Render Settings",  ImGuiKey_4,        KEY_MOD_CTRL) \
    X(toggleEditor,         "Windows & Panels",                "Toggle Editor",    ImGuiKey_F5,       KEY_MOD_NONE) \
    X(openPreferences,      "Windows & Panels",                "Preferences",      ImGuiKey_Comma,    KEY_MOD_CTRL) \
    X(deleteEntity,         "Entity",                          "Delete",           ImGuiKey_Delete,   KEY_MOD_NONE) \
    X(deselect,             "Entity",                          "Deselect",         ImGuiKey_Escape,   KEY_MOD_NONE) \
    X(duplicate,            "Entity",                          "Duplicate",        ImGuiKey_D,        KEY_MOD_CTRL) \
    X(focusSelected,        "View",                            "Focus Selected",   ImGuiKey_F,        KEY_MOD_NONE) \
    X(frameAll,             "View",                            "Frame All",        ImGuiKey_F,        KEY_MOD_SHIFT) \
    X(toggleOrthographic,   "View",                            "Orthographic",     ImGuiKey_Keypad5,  KEY_MOD_NONE) \
    X(gizmoSelect,          "Gizmo (disabled during fly-cam)", "Select",           ImGuiKey_Q,        KEY_MOD_NONE) \
    X(gizmoTranslate,       "Gizmo (disabled during fly-cam)", "Translate",        ImGuiKey_W,        KEY_MOD_NONE) \
    X(gizmoRotate,          "Gizmo (disabled during fly-cam)", "Rotate",           ImGuiKey_E,        KEY_MOD_NONE) \
    X(gizmoScale,           "Gizmo (disabled during fly-cam)", "Scale",            ImGuiKey_R,        KEY_MOD_NONE) \
    X(gizmoToggleSpace,     "Gizmo (disabled during fly-cam)", "Local/World",      ImGuiKey_X,        KEY_MOD_NONE)

/**
 * @brief All configurable editor keybinds, with their defaults.
 *
 * Expanded from VKM_EDITOR_KEYBINDS.
 */
struct EditorKeybinds {
#define VKM_KEYBIND_FIELD(field, group, label, key, mods) KeyBind field = { key, mods };
    VKM_EDITOR_KEYBINDS(VKM_KEYBIND_FIELD)
#undef VKM_KEYBIND_FIELD
};

/**
 * @brief One keybind's identity: where Preferences shows it, and how it persists.
 */
struct KeybindEntry {
    const char* group;                ///< Preferences section heading.
    const char* label;                ///< Row label in Preferences.
    const char* jsonName;             ///< Persisted key; the field's name.
    KeyBind EditorKeybinds::* field;  ///< Member the row edits.
};

/**
 * @brief Every configurable keybind, in Preferences display order.
 *
 * Expanded from VKM_EDITOR_KEYBINDS.
 */
inline constexpr KeybindEntry KEYBINDS[] = {
#define VKM_KEYBIND_ROW(field, group, label, key, mods) { group, label, #field, &EditorKeybinds::field },
    VKM_EDITOR_KEYBINDS(VKM_KEYBIND_ROW)
#undef VKM_KEYBIND_ROW
};

/**
 * @brief Check whether a keybind was just pressed this frame.
 *
 * Modifiers must match exactly, so Ctrl+Z does not fire under Ctrl+Shift+Z.
 *
 * @param bind Unbound (ImGuiKey_None) never matches.
 * @param repeat Fire again at the key-repeat rate while held; false for a toggle.
 * @return true on the press frame with matching modifiers.
 */
inline bool isPressed(const KeyBind& bind, bool repeat = true) {
    if (bind.key == ImGuiKey_None) return false;
    if (!ImGui::IsKeyPressed(bind.key, repeat)) return false;

    const ImGuiIO& io = ImGui::GetIO();
    if (((bind.mods & KEY_MOD_CTRL)  != 0) != io.KeyCtrl)  return false;
    if (((bind.mods & KEY_MOD_SHIFT) != 0) != io.KeyShift) return false;
    if (((bind.mods & KEY_MOD_ALT)   != 0) != io.KeyAlt)   return false;

    return true;
}

/**
 * @brief Format a label for a keybind (e.g. "Ctrl+D", "F5", "W").
 *
 * @param bind Rendered into text.
 * @param buf Receives the label.
 * @param bufSize Capacity of @p buf; the label is truncated to fit.
 * @return @p buf.
 */
const char* getKeyBindLabel(const KeyBind& bind, char* buf, size_t bufSize);

/**
 * @brief Value-returning keybind label, for inline use as a MenuItem shortcut.
 *
 * The temporary lives to the end of the full expression, which is enough:
 * ImGui renders the shortcut during the call rather than storing the pointer.
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

} // namespace Vkm::Engine
