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
#include "ecs/component/animator.h"
#include "ecs/component/name.h"
#include "ecs/component/reflection_probe.h"
#include "ecs/component/irradiance_volume.h"
#include "ecs/component/decal.h"
#include "ecs/component/particle_emitter.h"
#include "ecs/component/ui_canvas.h"
#include "ecs/component/ui_element.h"
#include "ecs/component/ui_image.h"
#include "ecs/component/ui_text.h"
#include "ecs/component/ui_button.h"

namespace Vkm::Engine {

bool drawVec3Control(const char* label, float* values,
                     float resetValue, float speed) {
    bool changed = false;
    ImGui::PushID(label);

    float lineHeight = ImGui::GetFrameHeight();
    ImVec2 buttonSize(lineHeight + 2.0f, lineHeight);
    // Floored, because the share left over goes to zero on a narrow panel and
    // the three drags disappear entirely - a Transform card reduced to a row of
    // axis buttons with no number to read or drag. Overflowing the column is the
    // lesser failure, and the panel is resizable.
    float inputWidth = std::max((ImGui::GetContentRegionAvail().x - EditorStyle::labelWidth()
                                 - buttonSize.x * 3 - ImGui::GetStyle().ItemSpacing.x * 5) / 3.0f,
                                ImGui::GetFontSize() * 2.5f);

    // Column measured from the row's start (card-indent aware), like
    // drawPropertyLabel.
    const float startX = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(startX + EditorStyle::labelWidth());

    static const struct {
        const char*   button;
        const char*   drag;
        const ImVec4& color;
        const ImVec4& hover;
    } axes[3] = {
        { "X", "##X", EditorStyle::AXIS_X, EditorStyle::AXIS_X_HOV },
        { "Y", "##Y", EditorStyle::AXIS_Y, EditorStyle::AXIS_Y_HOV },
        { "Z", "##Z", EditorStyle::AXIS_Z, EditorStyle::AXIS_Z_HOV },
    };

    for (int i = 0; i < 3; ++i) {
        ImGui::PushStyleColor(ImGuiCol_Button, axes[i].color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, axes[i].hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, axes[i].hover);
        if (ImGui::Button(axes[i].button, buttonSize)) { values[i] = resetValue; changed = true; }
        ImGui::PopStyleColor(3);
        ImGui::SameLine(0, 2);
        ImGui::SetNextItemWidth(inputWidth);
        changed |= ImGui::DragFloat(axes[i].drag, &values[i], speed, 0.0f, 0.0f, "%.2f");
        if (i < 2) ImGui::SameLine(0, 6);
    }

    ImGui::PopID();
    return changed;
}

void drawPropertyLabel(const char* label) {
    ImGui::AlignTextToFramePadding();

    // The label lives in a fixed column measured from the ROW's start - i.e.
    // including any card indent. (Measured from the window edge, a wide label
    // inside an indented card slid underneath its widget: "Focus Distance"
    // behind the slider.) A long label ellipsizes inside the column instead of
    // pushing the widget.
    const float startX = ImGui::GetCursorPosX();
    const float colW   = EditorStyle::labelWidth();
    const float maxW   = colW - ImGui::GetStyle().ItemSpacing.x;

    if (ImGui::CalcTextSize(label).x <= maxW) {
        ImGui::TextUnformatted(label);
    } else {
        const float dotsW = ImGui::CalcTextSize("..").x;
        char clipped[96];
        size_t n = 0;
        for (const char* c = label; *c && n < sizeof(clipped) - 4; ++c) {
            clipped[n] = *c;
            clipped[n + 1] = '\0';
            if (ImGui::CalcTextSize(clipped).x + dotsW > maxW) break;
            ++n;
        }
        snprintf(clipped + n, sizeof(clipped) - n, "..");
        ImGui::TextUnformatted(clipped);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", label);
    }

    ImGui::SameLine(startX + colW);
    ImGui::SetNextItemWidth(-1);
}

namespace {
struct CardState {
    ImVec4 accent;
    float  startY = 0.0f;   // body top, screen-space y
    float  lineX  = 0.0f;   // left accent-line x, screen-space
    bool   open   = false;
};
// Accessor instead of a bare global: the stack enforces balanced begin/end
// pairs while keeping the lifetime explicit. thread_local because the only
// context where it is valid is the ImGui-owning thread.
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

bool drawEasingCombo(const char* id, EasingFunction& easing) {
    const int current = Easing::indexOf(easing);
    ImGui::SetNextItemWidth(-1);
    if (!ImGui::BeginCombo(id, Easing::EASINGS[current].name)) return false;

    bool changed = false;
    for (int i = 0; i < Easing::EASING_COUNT; ++i) {
        const bool selected = (i == current);
        if (ImGui::Selectable(Easing::EASINGS[i].name, selected)) {
            easing = Easing::byIndex(i);
            changed = true;
        }
        if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
    return changed;
}

bool matchesFilter(const char* text, const char* filter) {
    if (!filter || !filter[0]) return true;
    for (const char* p = text; *p; ++p) {
        const char* s = filter;
        const char* t = p;
        while (*s && *t && tolower(static_cast<unsigned char>(*s)) ==
                            tolower(static_cast<unsigned char>(*t))) { ++s; ++t; }
        if (!*s) return true;
    }
    return false;
}

namespace {

// What kind of thing an entity is, answered once for both the label the
// hierarchy shows and the glyph beside it. The two used to be separate ladders
// over the same components and had already diverged - the icons grew a
// UIElement row the names never got, so a bare UI widget drew the widget glyph
// beside the text "Entity 12", and the inspector's "name this entity" button
// baked that string into a real Name.
//
// Stays a hand-written ladder rather than a component->row table: a Light's
// answer comes from its inner type, and a Mesh carrying an Animation reads
// "Animated Mesh" while keeping the plain mesh glyph. Order is precedence.
struct EntityLabel {
    const char* name;
    EditorIcon  icon;
};

EntityLabel entityLabelOf(const Scene& scene, EntityId id) {
    if (scene.has<Camera>(id)) return {"Camera", EditorIcon::Camera};
    if (scene.has<Light>(id)) {
        switch (scene.get<Light>(id).type) {
            case LightType::Directional: return {"Dir Light",   EditorIcon::LightDir};
            case LightType::Point:       return {"Point Light", EditorIcon::LightPoint};
            case LightType::Spot:        return {"Spot Light",  EditorIcon::LightSpot};
            case LightType::Rect:        return {"Rect Light",  EditorIcon::LightRect};
            case LightType::Disk:        return {"Disk Light",  EditorIcon::LightDisk};
            case LightType::Count:       break;  // enum-size sentinel, never stored
        }
        return {"Light", EditorIcon::LightPoint};
    }
    // Before Mesh: an entity carrying an Animator is the rig whatever else it
    // carries, and its meshes are the entities under it.
    if (scene.has<Animator>(id)) return {"Rig", EditorIcon::Anim};
    if (scene.has<Mesh>(id)) {
        return {scene.has<Animation>(id) ? "Animated Mesh" : "Mesh", EditorIcon::Mesh};
    }
    if (scene.has<Animation>(id))        return {"Animation", EditorIcon::Anim};
    if (scene.has<ReflectionProbe>(id))  return {"Probe",     EditorIcon::Probe};
    if (scene.has<IrradianceVolume>(id)) return {"GI Volume", EditorIcon::Volume};
    if (scene.has<Decal>(id))            return {"Decal",     EditorIcon::Decal};
    if (scene.has<ParticleEmitter>(id))  return {"Emitter",   EditorIcon::Particle};
    if (scene.has<UIButton>(id))         return {"Button",    EditorIcon::UIButton};
    if (scene.has<UIText>(id))           return {"Text",      EditorIcon::UIText};
    if (scene.has<UIImage>(id))          return {"Panel",     EditorIcon::UIImage};
    if (scene.has<UICanvas>(id))         return {"Canvas",    EditorIcon::UICanvas};
    if (scene.has<UIElement>(id))        return {"Widget",    EditorIcon::UIWidget};
    return {"Entity", EditorIcon::Entity};
}

} // namespace

void getEntityDisplayName(const Scene& scene, EntityId id,
                          char* buf, size_t bufSize) {
    if (scene.has<Name>(id)) {
        const auto& name = scene.get<Name>(id);
        if (name.value[0] != '\0') {
            snprintf(buf, bufSize, "%s", name.value);
            return;
        }
    }
    snprintf(buf, bufSize, "%s %u", entityLabelOf(scene, id).name, id.index);
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
    return entityLabelOf(scene, id).icon;
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

bool iconMenuItem(EditorIcon icon, const char* label, const char* shortcut, bool enabled) {
    char padded[96];
    iconPaddedLabel(padded, sizeof(padded), label, nullptr);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::MenuItem(padded, shortcut, false, enabled);
    drawRowGlyph(icon, p.x + 4.0f, p, ImGui::GetItemRectSize().y);
    return pressed;
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

} // namespace Vkm::Engine
