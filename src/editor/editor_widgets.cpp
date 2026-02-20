#include "editor_common.h"

namespace Engine {

// Axis colors (shared with drawVec3Control)
static const ImVec4 kAxisRed      = ImVec4(0.80f, 0.18f, 0.18f, 1.00f);
static const ImVec4 kAxisGreen    = ImVec4(0.30f, 0.70f, 0.20f, 1.00f);
static const ImVec4 kAxisBlue     = ImVec4(0.20f, 0.35f, 0.85f, 1.00f);
static const ImVec4 kAxisRedHov   = ImVec4(0.90f, 0.28f, 0.28f, 1.00f);
static const ImVec4 kAxisGreenHov = ImVec4(0.40f, 0.80f, 0.30f, 1.00f);
static const ImVec4 kAxisBlueHov  = ImVec4(0.30f, 0.45f, 0.95f, 1.00f);

static constexpr float kLabelWidth = 100.0f;
bool EditorSystem::drawVec3Control(const char* label, float* values,
                                    float resetValue, float speed) {
    bool changed = false;
    ImGui::PushID(label);

    float lineHeight = ImGui::GetFrameHeight();
    ImVec2 buttonSize(lineHeight + 2.0f, lineHeight);
    float inputWidth = (ImGui::GetContentRegionAvail().x - kLabelWidth
                        - buttonSize.x * 3 - ImGui::GetStyle().ItemSpacing.x * 5) / 3.0f;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(kLabelWidth);

    ImGui::PushStyleColor(ImGuiCol_Button, kAxisRed);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAxisRedHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAxisRed);
    if (ImGui::Button("X", buttonSize)) { values[0] = resetValue; changed = true; }
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, 2);
    ImGui::SetNextItemWidth(inputWidth);
    changed |= ImGui::DragFloat("##X", &values[0], speed, 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(0, 6);

    ImGui::PushStyleColor(ImGuiCol_Button, kAxisGreen);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAxisGreenHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAxisGreen);
    if (ImGui::Button("Y", buttonSize)) { values[1] = resetValue; changed = true; }
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, 2);
    ImGui::SetNextItemWidth(inputWidth);
    changed |= ImGui::DragFloat("##Y", &values[1], speed, 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(0, 6);

    ImGui::PushStyleColor(ImGuiCol_Button, kAxisBlue);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAxisBlueHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAxisBlue);
    if (ImGui::Button("Z", buttonSize)) { values[2] = resetValue; changed = true; }
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, 2);
    ImGui::SetNextItemWidth(inputWidth);
    changed |= ImGui::DragFloat("##Z", &values[2], speed, 0.0f, 0.0f, "%.2f");

    ImGui::PopID();
    return changed;
}

void EditorSystem::drawPropertyLabel(const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(kLabelWidth);
    ImGui::SetNextItemWidth(-1);
}

bool EditorSystem::drawRemoveButton(const char* compLabel, uint32_t entityIdx) {
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 20);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.3f, 0.3f, 1.0f));
    char id[32];
    snprintf(id, sizeof(id), "x##Rem%s%u", compLabel, entityIdx);
    bool clicked = ImGui::SmallButton(id);
    if (ImGui::IsItemHovered()) {
        ImGui::PopStyleColor();
        ImGui::SetTooltip("Remove %s", compLabel);
    } else {
        ImGui::PopStyleColor();
    }
    return clicked;
}

bool EditorSystem::matchesFilter(const char* text, const char* filter) {
    for (const char* p = text; *p; ++p) {
        const char* s = filter;
        const char* t = p;
        while (*s && *t && tolower(static_cast<unsigned char>(*s)) ==
                            tolower(static_cast<unsigned char>(*t))) { ++s; ++t; }
        if (!*s) return true;
    }
    return false;
}

void EditorSystem::getEntityDisplayName(const Scene& scene, EntityId id,
                                         char* buf, size_t bufSize) const {
    if (scene.has<Name>(id)) {
        const auto& name = scene.get<Name>(id);
        if (name.value[0] != '\0') {
            snprintf(buf, bufSize, "%s", name.value);
            return;
        }
    }
    const char* typeName = "Entity";
    if (scene.has<Camera>(id))    typeName = "Camera";
    else if (scene.has<Light>(id)) {
        auto& l = scene.get<Light>(id);
        typeName = l.type == LightType::Directional ? "Dir Light" :
                   l.type == LightType::Point ? "Point Light" : "Spot Light";
    }
    else if (scene.has<Mesh>(id)) typeName = scene.has<Animation>(id) ? "Animated Mesh" : "Mesh";
    else if (scene.has<Animation>(id)) typeName = "Animation";
    snprintf(buf, bufSize, "%s %u", typeName, id.index);
}

void EditorSystem::getEntityIcon(const Scene& scene, EntityId id,
                                  char* buf, size_t bufSize) const {
    const char* icon = "[ ]";
    if (scene.has<Camera>(id))    icon = "[C]";
    else if (scene.has<Light>(id)) {
        auto& l = scene.get<Light>(id);
        if (l.type == LightType::Directional) icon = "[D]";
        else if (l.type == LightType::Point)  icon = "[P]";
        else icon = "[S]";
    }
    else if (scene.has<Mesh>(id))      icon = "[M]";
    else if (scene.has<Animation>(id)) icon = "[A]";
    snprintf(buf, bufSize, "%s", icon);
}

} // namespace Engine
