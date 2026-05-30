#include "ui/editor_widgets.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>

#include <imgui.h>

#include "ui/editor_style.h"
#include "ecs/scene.h"
#include "ecs/component/mesh.h"
#include "ecs/component/light.h"
#include "ecs/component/camera.h"
#include "ecs/component/animation.h"
#include "ecs/component/name.h"
#include "system/render/render_view.h"   // EnvironmentConfig (singleton glyph)

namespace Engine {

namespace {
const ImVec4& AXIS_RED       = EditorStyle::AXIS_X;
const ImVec4& AXIS_GREEN     = EditorStyle::AXIS_Y;
const ImVec4& AXIS_BLUE      = EditorStyle::AXIS_Z;
const ImVec4& AXIS_RED_HOV   = EditorStyle::AXIS_X_HOV;
const ImVec4& AXIS_GREEN_HOV = EditorStyle::AXIS_Y_HOV;
const ImVec4& AXIS_BLUE_HOV  = EditorStyle::AXIS_Z_HOV;
constexpr float LABEL_WIDTH  = EditorStyle::LABEL_WIDTH;
}

bool drawVec3Control(const char* label, float* values,
                     float resetValue, float speed) {
    bool changed = false;
    ImGui::PushID(label);

    float lineHeight = ImGui::GetFrameHeight();
    ImVec2 buttonSize(lineHeight + 2.0f, lineHeight);
    float inputWidth = (ImGui::GetContentRegionAvail().x - LABEL_WIDTH
                        - buttonSize.x * 3 - ImGui::GetStyle().ItemSpacing.x * 5) / 3.0f;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(LABEL_WIDTH);

    ImGui::PushStyleColor(ImGuiCol_Button, AXIS_RED);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AXIS_RED_HOV);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, AXIS_RED);
    if (ImGui::Button("X", buttonSize)) { values[0] = resetValue; changed = true; }
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, 2);
    ImGui::SetNextItemWidth(inputWidth);
    changed |= ImGui::DragFloat("##X", &values[0], speed, 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(0, 6);

    ImGui::PushStyleColor(ImGuiCol_Button, AXIS_GREEN);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AXIS_GREEN_HOV);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, AXIS_GREEN);
    if (ImGui::Button("Y", buttonSize)) { values[1] = resetValue; changed = true; }
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, 2);
    ImGui::SetNextItemWidth(inputWidth);
    changed |= ImGui::DragFloat("##Y", &values[1], speed, 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(0, 6);

    ImGui::PushStyleColor(ImGuiCol_Button, AXIS_BLUE);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AXIS_BLUE_HOV);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, AXIS_BLUE);
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
    // Labels that exceed the reserved column don't get truncated - the next
    // item just starts after the actual text width plus a padding. Short
    // labels still align at LABEL_WIDTH so the inspector reads as a column.
    const float lw = ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(std::max(LABEL_WIDTH, lw));
    ImGui::SetNextItemWidth(-1);
}

namespace {
struct CardState {
    ImVec4 accent;
    float  startY = 0.0f;   // body top, screen-space y
    float  lineX  = 0.0f;   // left accent-line x, screen-space
    bool   open   = false;
};
// Accessor instead of a bare global: keeps the stack a single instance
// (cards never nest deeply, but the stack still enforces balanced
// begin/end pairs) while making the lifetime explicit. thread_local
// so this is honest about the only context where the stack is valid:
// the ImGui-owning thread.
std::vector<CardState>& cardStack() {
    thread_local std::vector<CardState> s;
    return s;
}
constexpr float CARD_INDENT = 14.0f;

// Tinted, accent-stripped CollapsingHeader (no body/end pairing). File-local -
// only beginComponentCard below uses it.
bool styledCollapsingHeader(const char* title, const ImVec4& accent,
                            bool defaultOpen) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth
                             | ImGuiTreeNodeFlags_AllowOverlap
                             | ImGuiTreeNodeFlags_FramePadding;
    if (defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;

    ImGui::PushStyleColor(ImGuiCol_Header,        EditorStyle::CARD_HEADER);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, EditorStyle::CARD_HEADER_HOV);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  EditorStyle::CARD_HEADER_ACT);
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
}  // namespace

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
        ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::DANGER);
        if (ImGui::SmallButton("x")) *removeClicked = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove component");
        ImGui::PopStyleColor(2);
    }

    CardState st;
    st.accent = accent;
    st.open   = open;
    st.lineX  = rMin.x + CARD_INDENT * 0.5f;
    if (open) {
        ImGui::Indent(CARD_INDENT);
        ImGui::Spacing();
        st.startY = ImGui::GetCursorScreenPos().y;
    }
    cardStack().push_back(st);
    return open;
}

void endComponentCard() {
    CardState st = cardStack().back();
    cardStack().pop_back();

    if (st.open) {
        ImGui::Spacing();
        const float endY = ImGui::GetCursorScreenPos().y;
        ImGui::Unindent(CARD_INDENT);
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

bool drawEasingCombo(const char* id, EasingFunction& easing) {
    int idx = Easing::indexOf(easing);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo(id, &idx, Easing::EASING_NAMES, Easing::EASING_COUNT)) {
        easing = Easing::byIndex(idx);
        return true;
    }
    return false;
}

namespace {
// Shared RenderMode catalogue. Both the Environment Inspector picker
// and the viewport toolbar's quick-toggle popup read from this table;
// adding a new mode is one place. Items grouped by user intent so the
// diagnostic visualisations don't sit at the bottom of a flat list.
struct ModeEntry {
    RenderMode  mode;
    const char* label;
    const char* hint;
};
struct ModeGroup {
    const char* title;
    const ModeEntry* entries;
    int count;
};
constexpr ModeEntry MODE_SHADING[] = {
    {RenderMode::Default,             "Shaded",                "full PBR pipeline"},
    {RenderMode::Unlit,                "Unlit",                "albedo + emission only"},
    {RenderMode::Wireframe,            "Wireframe",            "unlit lines, no fill"},
    {RenderMode::WireframeOverShaded,  "Wireframe Over Shaded","shaded scene + line overlay"},
};
constexpr ModeEntry MODE_MATERIAL[] = {
    {RenderMode::AlbedoOnly,           "Albedo Only",          "base colour with no lighting"},
    {RenderMode::Roughness,            "Roughness",            "sampled roughness as grayscale"},
    {RenderMode::Metallic,             "Metallic",             "sampled metallic factor"},
    {RenderMode::Emission,             "Emission",             "raw emission (linear HDR)"},
    {RenderMode::Normals,              "Normals",              "world-space normal as RGB"},
    {RenderMode::TangentSpace,         "Tangent Space",        "world-space tangent as RGB"},
};
constexpr ModeEntry MODE_LIGHTING[] = {
    {RenderMode::LightingOnly,         "Lighting Only",        "PBR with neutral material"},
    {RenderMode::AOOnly,               "AO Only",              "GTAO factor as grayscale"},
};
constexpr ModeEntry MODE_GEOMETRY[] = {
    {RenderMode::Depth,                "Depth",                "distance from camera (white = far)"},
    {RenderMode::WorldPosition,        "World Position",       "fract(worldPos) (1m grid)"},
    {RenderMode::UV,                   "UV",                   "UV channel 0 as red/green"},
};
constexpr ModeEntry MODE_DIAGNOSTIC[] = {
    {RenderMode::Overdraw,             "Overdraw",             "additive heatmap of shaded fragments"},
    {RenderMode::BatchId,              "Batch ID",             "hashed RGB per draw batch"},
    {RenderMode::LightComplexity,      "Light Complexity",     "count of contributing lights (cold -> hot)"},
};
constexpr ModeGroup MODE_GROUPS[] = {
    {"Shading",     MODE_SHADING,    IM_ARRAYSIZE(MODE_SHADING)},
    {"Material",    MODE_MATERIAL,   IM_ARRAYSIZE(MODE_MATERIAL)},
    {"Lighting",    MODE_LIGHTING,   IM_ARRAYSIZE(MODE_LIGHTING)},
    {"Geometry",    MODE_GEOMETRY,   IM_ARRAYSIZE(MODE_GEOMETRY)},
    {"Diagnostic",  MODE_DIAGNOSTIC, IM_ARRAYSIZE(MODE_DIAGNOSTIC)},
};
const ModeEntry* findModeEntry(RenderMode m) {
    for (const auto& g : MODE_GROUPS) {
        for (int i = 0; i < g.count; ++i) {
            if (g.entries[i].mode == m) return &g.entries[i];
        }
    }
    return &MODE_SHADING[0];  // Default
}
} // namespace

const char* renderModeLabel(RenderMode mode) {
    return findModeEntry(mode)->label;
}

bool drawRenderModeMenuBody(RenderMode& mode) {
    bool changed = false;
    for (size_t gi = 0; gi < IM_ARRAYSIZE(MODE_GROUPS); ++gi) {
        const auto& g = MODE_GROUPS[gi];
        if (gi > 0) ImGui::Separator();
        ImGui::TextDisabled("%s", g.title);
        for (int i = 0; i < g.count; ++i) {
            const auto& e = g.entries[i];
            const bool selected = (mode == e.mode);
            if (ImGui::Selectable(e.label, selected)) {
                mode = e.mode;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", e.hint);
            if (selected) ImGui::SetItemDefaultFocus();
        }
    }
    return changed;
}

bool drawRenderModeCombo(const char* id, RenderMode& mode) {
    bool changed = false;
    const ModeEntry* current = findModeEntry(mode);
    if (ImGui::BeginCombo(id, current->label)) {
        changed = drawRenderModeMenuBody(mode);
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen(id))
        ImGui::SetTooltip("%s - %s", current->label, current->hint);
    return changed;
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

namespace {
// One source of truth for the entity-row glyph size, so the reserved
// label space and the drawn icon always agree.
float rowIconRadius() { return ImGui::GetFontSize() * 0.62f; }

// Leading spaces that clear the glyph, so a row's text starts to the
// right of the icon drawn into that gap. An optional id keeps ImGui ids
// stable when names collide.
void iconPaddedLabel(char* out, size_t n, const char* name,
                     const char* idStr) {
    const float sw = ImGui::CalcTextSize(" ").x;
    int pad = (sw > 0.0f)
        ? static_cast<int>((rowIconRadius() * 2.0f + 6.0f) / sw) + 1 : 4;
    if (pad < 2)  pad = 2;
    if (pad > 18) pad = 18;
    char sp[20];
    for (int i = 0; i < pad; ++i) sp[i] = ' ';
    sp[pad] = '\0';
    if (idStr) snprintf(out, n, "%s%s###%s", sp, name, idStr);
    else       snprintf(out, n, "%s%s", sp, name);
}
void drawRowGlyph(EditorIcon ic, float startX, ImVec2 rmin, float rh) {
    const float iconR = rowIconRadius();
    drawEditorIcon(ImGui::GetWindowDrawList(), ic,
        ImVec2(startX + iconR, rmin.y + rh * 0.5f), iconR,
        ImGui::GetColorU32(ImGuiCol_Text));
}
}

EditorIcon entityIconKind(const Scene& scene, EntityId id) {
    if (scene.has<EnvironmentConfig>(id)) return EditorIcon::Environment;
    if (scene.has<Camera>(id)) return EditorIcon::Camera;
    if (scene.has<Light>(id)) {
        const auto& l = scene.get<Light>(id);
        if (l.type == LightType::Directional) return EditorIcon::LightDir;
        if (l.type == LightType::Point)       return EditorIcon::LightPoint;
        return EditorIcon::LightSpot;
    }
    if (scene.has<Mesh>(id))      return EditorIcon::Mesh;
    if (scene.has<Animation>(id)) return EditorIcon::Anim;
    return EditorIcon::Entity;
}

void inlineIcon(EditorIcon icon, float size, ImU32 color) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    drawEditorIcon(ImGui::GetWindowDrawList(), icon,
        ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size * 0.40f, color);
}

bool entityTreeNode(const void* idPtr, ImGuiTreeNodeFlags flags,
                    EditorIcon icon, const char* name) {
    char label[96];
    iconPaddedLabel(label, sizeof(label), name, nullptr);
    const bool open = ImGui::TreeNodeEx(const_cast<void*>(idPtr), flags, "%s", label);
    const ImVec2 rmin = ImGui::GetItemRectMin();
    const float  rh   = ImGui::GetItemRectSize().y;
    drawRowGlyph(icon, rmin.x + ImGui::GetTreeNodeToLabelSpacing(), rmin, rh);
    return open;
}

bool entitySelectable(const char* idStr, bool selected,
                      EditorIcon icon, const char* name) {
    char label[96];
    iconPaddedLabel(label, sizeof(label), name, idStr);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Selectable(label, selected);
    drawRowGlyph(icon, p.x + 4.0f, p, ImGui::GetItemRectSize().y);
    return clicked;
}

} // namespace Engine
