#pragma once

#include <cstdint>

#include <imgui.h>

namespace Vkm::Engine {

/**
 * @brief Modifier bits, combined in KeyBind::mods.
 */
enum KeyMod : uint8_t {
    KEY_MOD_NONE  = 0,
    KEY_MOD_CTRL  = 1 << 0,
    KEY_MOD_SHIFT = 1 << 1,
    KEY_MOD_ALT   = 1 << 2,
};

inline constexpr ImGuiKey RENAME_KEY = ImGuiKey_F2;  ///< Renames what it is pressed over: the selected
                                                     ///< Hierarchy row, or a loaded asset tile of a kind
                                                     ///< that renames. Fixed, not a configurable keybind.

/**
 * @brief One ImGuiKey plus a KeyMod bitmask; ImGuiKey_None is unbound.
 */
struct KeyBind {
    ImGuiKey key  = ImGuiKey_None;
    uint8_t  mods = KEY_MOD_NONE;

    bool operator==(const KeyBind& o) const { return key == o.key && mods == o.mods; }
    bool operator!=(const KeyBind& o) const { return !(*this == o); }
};

/**
 * @brief Every configurable keybind, once, as (field, group, label, key, mods), in Preferences order.
 *
 * The persisted key is the field's name, so a rename discards saved rebindings. Rows sharing a group
 * stay contiguous: PreferencesPanel starts a heading whenever the group changes.
 */
#define VKM_EDITOR_KEYBINDS(X) \
    X(newScene,             "File",                            "New Scene",        ImGuiKey_N,            KEY_MOD_CTRL) \
    X(saveScene,            "File",                            "Save Scene",       ImGuiKey_S,            KEY_MOD_CTRL) \
    X(saveSceneAs,          "File",                            "Save Scene As",    ImGuiKey_S,            KEY_MOD_CTRL | KEY_MOD_SHIFT) \
    X(loadScene,            "File",                            "Open Scene",       ImGuiKey_O,            KEY_MOD_CTRL) \
    X(buildScripts,         "File",                            "Build Scripts",    ImGuiKey_B,            KEY_MOD_CTRL) \
    X(undo,                 "Edit",                            "Undo",             ImGuiKey_Z,            KEY_MOD_CTRL) \
    X(redo,                 "Edit",                            "Redo",             ImGuiKey_Z,            KEY_MOD_CTRL | KEY_MOD_SHIFT) \
    X(playStop,             "Play (live during a session)",    "Play / Stop",      ImGuiKey_P,            KEY_MOD_CTRL) \
    X(pauseResume,          "Play (live during a session)",    "Pause / Resume",   ImGuiKey_P,            KEY_MOD_CTRL | KEY_MOD_SHIFT) \
    X(ejectView,            "Play (live during a session)",    "Eject / Return",   ImGuiKey_F8,           KEY_MOD_NONE) \
    X(toggleEditor,         "Play (live during a session)",    "Toggle Editor",    ImGuiKey_F5,           KEY_MOD_NONE) \
    X(toggleHierarchy,      "Windows & Panels",                "Toggle Hierarchy", ImGuiKey_1,            KEY_MOD_CTRL) \
    X(toggleInspector,      "Windows & Panels",                "Toggle Inspector", ImGuiKey_2,            KEY_MOD_CTRL) \
    X(toggleAssets,         "Windows & Panels",                "Toggle Assets",    ImGuiKey_3,            KEY_MOD_CTRL) \
    X(toggleRenderSettings, "Windows & Panels",                "Render Settings",  ImGuiKey_4,            KEY_MOD_CTRL) \
    X(toggleTerrain,        "Windows & Panels",                "Toggle Terrain",   ImGuiKey_5,            KEY_MOD_CTRL) \
    X(showConsole,          "Windows & Panels",                "Console",          ImGuiKey_6,            KEY_MOD_CTRL) \
    X(openPreferences,      "Windows & Panels",                "Preferences",      ImGuiKey_Comma,        KEY_MOD_CTRL) \
    X(deleteEntity,         "Entity",                          "Delete",           ImGuiKey_Delete,       KEY_MOD_NONE) \
    X(deselect,             "Entity",                          "Deselect",         ImGuiKey_Escape,       KEY_MOD_NONE) \
    X(duplicate,            "Entity",                          "Duplicate",        ImGuiKey_D,            KEY_MOD_CTRL) \
    X(copyEntity,           "Entity",                          "Copy",             ImGuiKey_C,            KEY_MOD_CTRL) \
    X(cutEntity,            "Entity",                          "Cut",              ImGuiKey_X,            KEY_MOD_CTRL) \
    X(pasteEntity,          "Entity",                          "Paste",            ImGuiKey_V,            KEY_MOD_CTRL) \
    X(selectAll,            "Entity",                          "Select All",       ImGuiKey_A,            KEY_MOD_CTRL) \
    X(alignWithView,        "Entity",                          "Align With View",  ImGuiKey_F,            KEY_MOD_CTRL | KEY_MOD_SHIFT) \
    X(dropToGround,         "Entity",                          "Drop to Ground",   ImGuiKey_End,          KEY_MOD_NONE) \
    X(groupSelection,       "Entity",                          "Group",            ImGuiKey_G,            KEY_MOD_CTRL) \
    X(hideSelection,        "Entity",                          "Hide in View",     ImGuiKey_H,            KEY_MOD_NONE) \
    X(showAll,              "Entity",                          "Show All in View", ImGuiKey_H,            KEY_MOD_ALT) \
    X(focusSelected,        "View",                            "Focus Selected",   ImGuiKey_F,            KEY_MOD_NONE) \
    X(frameAll,             "View",                            "Frame All",        ImGuiKey_F,            KEY_MOD_SHIFT) \
    X(toggleOrthographic,   "View",                            "Orthographic",     ImGuiKey_Keypad5,      KEY_MOD_NONE) \
    X(gizmoSelect,          "Gizmo (disabled during fly-cam)", "Select",           ImGuiKey_Q,            KEY_MOD_NONE) \
    X(gizmoTranslate,       "Gizmo (disabled during fly-cam)", "Move",             ImGuiKey_W,            KEY_MOD_NONE) \
    X(gizmoRotate,          "Gizmo (disabled during fly-cam)", "Rotate",           ImGuiKey_E,            KEY_MOD_NONE) \
    X(gizmoScale,           "Gizmo (disabled during fly-cam)", "Scale",            ImGuiKey_R,            KEY_MOD_NONE) \
    X(gizmoToggleSpace,     "Gizmo (disabled during fly-cam)", "Local/World",      ImGuiKey_X,            KEY_MOD_NONE) \
    X(terrainBrushSmaller,  "Terrain Brush",                   "Smaller Brush",    ImGuiKey_LeftBracket,  KEY_MOD_NONE) \
    X(terrainBrushLarger,   "Terrain Brush",                   "Larger Brush",     ImGuiKey_RightBracket, KEY_MOD_NONE)

/**
 * @brief Every configurable keybind as a field holding its default; expanded from VKM_EDITOR_KEYBINDS.
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
    const char*               group;     ///< Preferences section heading.
    const char*               label;     ///< Row label in Preferences.
    const char*               jsonName;  ///< Persisted key; the field's name.
    KeyBind EditorKeybinds::* field;     ///< Member the row edits.
};

inline constexpr KeybindEntry KEYBINDS[] = {
#define VKM_KEYBIND_ROW(field, group, label, key, mods) { group, label, #field, &EditorKeybinds::field },
    VKM_EDITOR_KEYBINDS(VKM_KEYBIND_ROW)
#undef VKM_KEYBIND_ROW
};  ///< Every configurable keybind, in Preferences display order; expanded from VKM_EDITOR_KEYBINDS.

/**
 * @brief Whether @p bind was pressed this frame with exactly its modifiers, so Ctrl+Z misses Ctrl+Shift+Z.
 *
 * @param bind Unbound (ImGuiKey_None) never matches.
 * @param repeat Fire again at the key-repeat rate while held; false for a toggle.
 * @return true on the press frame, and on each repeat when @p repeat.
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
 * @param bind Reads "None" when unbound.
 * @param buf Receives the label.
 * @param bufSize Capacity of @p buf; the label is truncated to fit.
 * @return @p buf.
 */
const char* getKeyBindLabel(const KeyBind& bind, char* buf, size_t bufSize);

/**
 * @brief Value-returning keybind label, for inline use as a MenuItem shortcut.
 *
 * The temporary outlives the call, and ImGui does not keep the pointer.
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
