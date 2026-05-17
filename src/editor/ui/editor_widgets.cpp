#include "ui/editor_widgets.h"
#include "ui/editor_style.h"

#include <imgui.h>
#include <cstdio>
#include <cctype>
#include <vector>

#include "ecs/scene.h"
#include "ecs/component/mesh.h"
#include "ecs/component/light.h"
#include "ecs/component/camera.h"
#include "ecs/component/animation.h"
#include "ecs/component/name.h"

namespace Engine {

namespace {
    const ImVec4& kAxisRed      = EditorStyle::AXIS_X;
    const ImVec4& kAxisGreen    = EditorStyle::AXIS_Y;
    const ImVec4& kAxisBlue     = EditorStyle::AXIS_Z;
    const ImVec4& kAxisRedHov   = EditorStyle::AXIS_X_HOV;
    const ImVec4& kAxisGreenHov = EditorStyle::AXIS_Y_HOV;
    const ImVec4& kAxisBlueHov  = EditorStyle::AXIS_Z_HOV;
    constexpr float kLabelWidth = EditorStyle::LABEL_WIDTH;
}

bool drawVec3Control(const char* label, float* values,
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

void drawPropertyLabel(const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(kLabelWidth);
    ImGui::SetNextItemWidth(-1);
}

namespace {
    struct CardState {
        ImVec4 accent;
        float  startY = 0.0f;   // body top, screen-space y
        float  lineX  = 0.0f;   // left accent-line x, screen-space
        bool   open   = false;
    };
    // ImGui is single-threaded; a plain stack is enough. Cards never nest in
    // the inspector but the stack keeps begin/end strictly balanced anyway.
    std::vector<CardState> g_cardStack;
    constexpr float kCardIndent = 14.0f;
}

bool styledCollapsingHeader(const char* title, const ImVec4& accent,
                            bool defaultOpen) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth
                             | ImGuiTreeNodeFlags_AllowOverlap
                             | ImGuiTreeNodeFlags_FramePadding;
    if (defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;

    ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.19f, 0.20f, 0.23f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.27f, 0.31f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.23f, 0.25f, 0.29f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 7));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    const bool open = ImGui::CollapsingHeader(title, flags);
    const ImVec2 rMin = ImGui::GetItemRectMin();
    const ImVec2 rMax = ImGui::GetItemRectMax();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    // Accent strip welded to the header's left edge.
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(rMin.x, rMin.y), ImVec2(rMin.x + 3.0f, rMax.y),
        ImGui::GetColorU32(accent));
    return open;
}

bool beginComponentCard(const char* title, const ImVec4& accent,
                        bool defaultOpen, bool* removeClicked) {
    ImGui::PushID(title);
    ImGui::Spacing();

    const bool open   = styledCollapsingHeader(title, accent, defaultOpen);
    const ImVec2 rMin = ImGui::GetItemRectMin();

    if (removeClicked) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x
                        + ImGui::GetCursorPosX() - 20);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::DANGER_HOV);
        if (ImGui::SmallButton("x")) *removeClicked = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove component");
        ImGui::PopStyleColor(2);
    }

    CardState st;
    st.accent = accent;
    st.open   = open;
    st.lineX  = rMin.x + kCardIndent * 0.5f;
    if (open) {
        ImGui::Indent(kCardIndent);
        ImGui::Spacing();
        st.startY = ImGui::GetCursorScreenPos().y;
    }
    g_cardStack.push_back(st);
    return open;
}

void endComponentCard() {
    CardState st = g_cardStack.back();
    g_cardStack.pop_back();

    if (st.open) {
        ImGui::Spacing();
        const float endY = ImGui::GetCursorScreenPos().y;
        ImGui::Unindent(kCardIndent);
        const ImU32 c = ImGui::GetColorU32(ImVec4(
            st.accent.x, st.accent.y, st.accent.z, 0.30f));
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(st.lineX, st.startY), ImVec2(st.lineX, endY), c, 2.0f);
    }
    ImGui::PopID();
    ImGui::Spacing();
}

namespace {
    // A crisp full-width accent rule under the current cursor - replaces the
    // flat default Separator so every panel/section reads the same way.
    void accentRule() {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float  w = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(p.x, p.y + 1.0f), ImVec2(p.x + w, p.y + 2.5f),
            ImGui::GetColorU32(EditorStyle::ACCENT));
        ImGui::Dummy(ImVec2(0.0f, 5.0f));
    }
}

void drawPanelTitle(const char* title) {
    ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::HEADER_TEXT);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    accentRule();
}

void drawSectionHeader(const char* title, const char* hint) {
    ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::HEADER_TEXT);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    if (hint && hint[0]) {
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::TextDisabled("%s", hint);
    }
    accentRule();
}

bool drawRemoveButton(const char* compLabel, uint32_t entityIdx) {
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

bool drawEasingCombo(const char* id, EasingFunction& easing) {
    int idx = Easing::indexOf(easing);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo(id, &idx, Easing::kEasingNames, Easing::kEasingCount)) {
        easing = Easing::byIndex(idx);
        return true;
    }
    return false;
}

bool matchesFilter(const char* text, const char* filter) {
    for (const char* p = text; *p; ++p) {
        const char* s = filter;
        const char* t = p;
        while (*s && *t && tolower(static_cast<unsigned char>(*s)) ==
                            tolower(static_cast<unsigned char>(*t))) { ++s; ++t; }
        if (!*s) return true;
    }
    return false;
}

void getEntityDisplayName(const Scene& scene, EntityId id,
                          char* buf, size_t bufSize) {
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

void getEntityIcon(const Scene& scene, EntityId id,
                   char* buf, size_t bufSize) {
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
