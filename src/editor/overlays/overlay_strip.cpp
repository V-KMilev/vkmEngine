#include "overlays/overlay_strip.h"

namespace Vkm::Engine::OverlayStrip {

bool begin(const char* id, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorStyle::OVERLAY_BG);
    ImGui::PushStyleColor(ImGuiCol_Border, EditorStyle::OVERLAY_EDGE);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad(), pad()));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap(), gap()));
    ImGuiChildFlags flags = ImGuiChildFlags_Borders;
    if (size.x <= 0.0f) flags |= ImGuiChildFlags_AutoResizeX;
    if (size.y <= 0.0f) flags |= ImGuiChildFlags_AutoResizeY;
    return ImGui::BeginChild(
        id,
        size,
        flags,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );
}

void end() {
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

} // namespace Vkm::Engine::OverlayStrip
