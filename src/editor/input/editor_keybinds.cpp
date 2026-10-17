#include "input/editor_keybinds.h"

#include <cstdio>
#include <cstring>

namespace Vkm::Engine {

namespace {

/// The key as it is printed on the keyboard, where ImGui spells it out: "[" not "LeftBracket".
const char* keyFaceName(ImGuiKey key) {
    switch (key) {
        case ImGuiKey_Escape:       return "Esc";
        case ImGuiKey_LeftBracket:  return "[";
        case ImGuiKey_RightBracket: return "]";
        case ImGuiKey_Comma:        return ",";
        case ImGuiKey_Period:       return ".";
        case ImGuiKey_Slash:        return "/";
        case ImGuiKey_Backslash:    return "\\";
        case ImGuiKey_Semicolon:    return ";";
        case ImGuiKey_Apostrophe:   return "'";
        case ImGuiKey_Minus:        return "-";
        case ImGuiKey_Equal:        return "=";
        case ImGuiKey_GraveAccent:  return "`";
        default:                    return ImGui::GetKeyName(key);
    }
}

} // namespace

const char* getKeyBindLabel(const KeyBind& bind, char* buf, size_t bufSize) {
    if (bind.key == ImGuiKey_None) {
        snprintf(buf, bufSize, "None");
        return buf;
    }

    char prefix[32] = {};
    if (bind.mods & KEY_MOD_CTRL)  std::strcat(prefix, "Ctrl+");
    if (bind.mods & KEY_MOD_SHIFT) std::strcat(prefix, "Shift+");
    if (bind.mods & KEY_MOD_ALT)   std::strcat(prefix, "Alt+");

    const char* keyName = keyFaceName(bind.key);
    snprintf(buf, bufSize, "%s%s", prefix, keyName);
    return buf;
}

} // namespace Vkm::Engine
