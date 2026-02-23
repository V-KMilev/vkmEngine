#include "editor_keybinds.h"

#include <cstdio>
#include <cstring>

namespace Engine {

const char* getKeyBindLabel(const KeyBind& bind, char* buf, size_t bufSize) {
    if (bind.key == ImGuiKey_None) {
        snprintf(buf, bufSize, "None");
        return buf;
    }

    char prefix[32] = {};
    if (bind.mods & KeyMod_Ctrl)  std::strcat(prefix, "Ctrl+");
    if (bind.mods & KeyMod_Shift) std::strcat(prefix, "Shift+");
    if (bind.mods & KeyMod_Alt)   std::strcat(prefix, "Alt+");

    const char* keyName = ImGui::GetKeyName(bind.key);
    snprintf(buf, bufSize, "%s%s", prefix, keyName);
    return buf;
}

} // namespace Engine
