#include "ui/editor_widgets.h"

#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "ui/style/editor_style.h"
#include "base/encoding/utf8.h"
#include "io/project/roots.h"
namespace Vkm::Engine {

namespace {

// The value's position in [lo, hi] as 0..1, mapped as ImGui maps a click (logarithmic too).
float sliderShare(ImGuiDataType type, const void* v, const void* lo, const void* hi, ImGuiSliderFlags flags) {
    double value = 0.0, low = 0.0, high = 0.0;
    if (type == ImGuiDataType_S32) {
        value = *static_cast<const int*>(v);
        low   = *static_cast<const int*>(lo);
        high  = *static_cast<const int*>(hi);
    } else {
        value = *static_cast<const float*>(v);
        low   = *static_cast<const float*>(lo);
        high  = *static_cast<const float*>(hi);
    }
    if (high == low) return 0.0f;
    double share = (value - low) / (high - low);
    if ((flags & ImGuiSliderFlags_Logarithmic) && low > 0.0 && high > 0.0 && value > 0.0)
        share = std::log(value / low) / std::log(high / low);
    return static_cast<float>(std::clamp(share, 0.0, 1.0));
}

} // namespace

bool sliderScalar(
    const char* id,
    ImGuiDataType type,
    void* v,
    const void* lo,
    const void* hi,
    const char* format,
    ImGuiSliderFlags flags
) {
    // Typed into, it is ImGui's own text field.
    if (ImGui::TempInputIsActive(ImGui::GetCurrentWindow()->GetID(id)))
        return ImGui::SliderScalar(id, type, v, lo, hi, format, flags);

    // ImGui draws the frame; grab and text are drawn here, text last.
    const ImVec4 clear(0.0f, 0.0f, 0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, clear);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, clear);
    ImGui::PushStyleColor(ImGuiCol_Text, clear);
    const bool changed = ImGui::SliderScalar(id, type, v, lo, hi, format, flags);
    ImGui::PopStyleColor(3);
    if (!ImGui::IsItemVisible()) return changed;

    // ImGui's grab track: two pixels in from each end, less the grab width (an int's one step).
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float pad   = 2.0f;
    const float track = (max.x - min.x) - pad * 2.0f;
    float grab = style.GrabMinSize;
    if (type == ImGuiDataType_S32) {
        const float steps = static_cast<float>(*static_cast<const int*>(hi) - *static_cast<const int*>(lo));
        if (steps >= 0.0f) grab = std::max(track / (steps + 1.0f), style.GrabMinSize);
    }
    grab = std::min(grab, track);
    const float edge = min.x + pad + grab * 0.5f
        + sliderShare(type, v, lo, hi, flags) * (track - grab);

    const bool lit = ImGui::IsItemActive() || ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec4 fill = ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab);
    fill.w *= lit ? 0.50f : 0.36f;
    draw->AddRectFilled(
        min,
        ImVec2(edge, max.y),
        ImGui::GetColorU32(fill),
        style.FrameRounding,
        ImDrawFlags_RoundCornersLeft
    );

    char text[64];
    const char* end = text + ImGui::DataTypeFormatString(text, IM_ARRAYSIZE(text), type, v, format);
    ImGui::RenderTextClipped(min, max, text, end, nullptr, ImVec2(0.5f, 0.5f));
    return changed;
}

bool beginCombo(const char* id, const char* preview, ImGuiComboFlags flags) {
    // The field's rect, taken before an open popup becomes the current window.
    const ImVec2 min   = ImGui::GetCursorScreenPos();
    const float  width = ImGui::CalcItemWidth();
    ImDrawList*  dl    = ImGui::GetWindowDrawList();
    const bool   open  = ImGui::BeginCombo(id, "", flags | ImGuiComboFlags_NoArrowButton);

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 max(min.x + width, min.y + ImGui::GetFrameHeight());
    const float side     = ImGui::GetFontSize();
    const float chevronX = max.x - style.FramePadding.x - side * 0.5f;
    const ImVec2 textMin(min.x + style.FramePadding.x, min.y + style.FramePadding.y);
    const ImVec2 textMax(chevronX - side * 0.5f - style.ItemInnerSpacing.x, max.y);
    // Into the field's own list, which an open popup is not.
    dl->PushClipRect(textMin, textMax, true);
    dl->AddText(textMin, ImGui::GetColorU32(ImGuiCol_Text), preview);
    dl->PopClipRect();
    const float midY  = (min.y + max.y) * 0.5f;
    const ImU32 faint = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    drawEditorIcon(dl, EditorIcon::ChevronDown, ImVec2(chevronX, midY), side * 0.32f, faint);
    return open;
}

bool beginSubmenu(const char* label, bool enabled) {
    ImDrawList* dl  = ImGui::GetWindowDrawList();
    const bool open = ImGui::BeginMenu(label, enabled);
    // ImGui's arrow is the last shape it adds to the menu's list, a triangle filled with an edge or
    // without one; it is hidden, and the chevron drawn where it stood.
    const int triangle = (dl->Flags & ImDrawListFlags_AntiAliasedFill) ? 6 : 3;
    for (int i = std::max(0, dl->VtxBuffer.Size - triangle); i < dl->VtxBuffer.Size; ++i) {
        dl->VtxBuffer[i].col &= ~IM_COL32_A_MASK;
    }
    const ImVec2 min  = ImGui::GetItemRectMin();
    const ImVec2 max  = ImGui::GetItemRectMax();
    const float  side = ImGui::GetFontSize();
    const ImU32  ink  = ImGui::GetColorU32(ImGuiCol_TextDisabled, enabled ? 1.0f : 0.5f);
    const ImVec2 mark(max.x - side * 0.5f, (min.y + max.y) * 0.5f);
    drawEditorIcon(dl, EditorIcon::ChevronRight, mark, side * 0.32f, ink);
    return open;
}

bool comboList(const char* id, int* index, const char* const* labels, int count) {
    const char* preview = (*index >= 0 && *index < count) ? labels[*index] : "?";
    if (!beginCombo(id, preview)) return false;
    bool changed = false;
    for (int i = 0; i < count; ++i) {
        const bool selected = (i == *index);
        if (ImGui::Selectable(labels[i], selected) && !selected) {
            *index  = i;
            changed = true;
        }
        if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
    return changed;
}

bool vec3Fields(float* values, float speed, float lo, float hi, const char* format) {
    // Three equal fields with no floor, so the row fits whatever the panel's width.
    bool changed = false;
    const float gap   = EditorStyle::px(4.0f);
    const float width = (ImGui::GetContentRegionAvail().x - gap * 2.0f) / 3.0f;
    const float bar   = EditorStyle::px(2.0f);
    static constexpr const char* LETTERS[3] = {"X", "Y", "Z"};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine(0.0f, gap);
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(width);
        changed |= ImGui::DragFloat("##v", &values[i], speed, lo, hi, format, PROP_CLAMP);
        ImGui::PopID();
        // The axis's color as a bar at the field's left edge, its letter faint inside.
        const ImVec2 min     = ImGui::GetItemRectMin();
        const ImVec2 max     = ImGui::GetItemRectMax();
        const float rounding = ImGui::GetStyle().FrameRounding;
        const ImU32 axis     = EditorStyle::axisU32(i);
        dl->AddRectFilled(min, ImVec2(min.x + bar, max.y), axis, rounding, ImDrawFlags_RoundCornersLeft);
        // The letter only where it clears the centered value; the bar alone says the axis otherwise.
        // The width at two places: near enough to place the letter, whatever the format.
        char value[32];
        std::snprintf(value, sizeof(value), "%.2f", values[i]);
        const float margin  = (max.x - min.x - ImGui::CalcTextSize(value).x) * 0.5f;
        const float letterX = bar + EditorStyle::px(4.0f);
        if (margin >= letterX + ImGui::CalcTextSize(LETTERS[i]).x + EditorStyle::px(2.0f)) {
            const ImVec2 letterAt(min.x + letterX, min.y + ImGui::GetStyle().FramePadding.y);
            dl->AddText(letterAt, EditorStyle::axisU32(i, 150), LETTERS[i]);
        }
    }
    return changed;
}

void drawPropertyLabel(const char* label) {
    ImGui::AlignTextToFramePadding();

    // Measured from the row's start, indent included, or a label in an indented card slides
    // under its widget. A long label ellipsizes.
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
            clipped[n]     = *c;
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

bool searchField(const char* id, char* buf, size_t size, float width, const char* hint) {
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
        && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F, ImGuiInputFlags_RouteFocused)) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(width);
    return ImGui::InputTextWithHint(id, hint, buf, size, ImGuiInputTextFlags_EscapeClearsAll);
}

void factRow(const char* label, const char* value) {
    drawPropertyLabel(label);
    ImGui::TextUnformatted(value);
}

namespace {
struct CardState {
    int  frame = 0;      ///< The ImGui frame that pushed it.
    bool open  = false;
};

// The cards begun and not yet ended; ImGui, and so every card, is on one thread.
std::vector<CardState>& cardStack() {
    static std::vector<CardState> s_cards;
    return s_cards;
}
// A function: px() needs an ImGui context, absent when file-scope initializers run.
float cardIndent() { return EditorStyle::px(14.0f); }

// The card's header: chevron, icon, title in the heading face, and a "..." for its menu.
// Open or shut lives in ImGui's storage, as a tree node's does.
bool cardHeader(const char* title, EditorIcon icon, bool defaultOpen, const char* menu) {
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID openId = ImGui::GetID("##open");
    bool open = storage->GetBool(openId, defaultOpen);

    // 26 px at the 14 px font.
    const ImVec2 min    = ImGui::GetCursorScreenPos();
    const float  width  = ImGui::GetContentRegionAvail().x;
    const float  height = EditorStyle::px(26.0f);
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InvisibleButton("##header", ImVec2(width, height))) {
        open = !open;
        storage->SetBool(openId, open);
    }
    const bool hovered = ImGui::IsItemHovered();
    const bool held    = ImGui::IsItemActive();
    if (menu) ImGui::OpenPopupOnItemClick(menu, ImGuiPopupFlags_MouseButtonRight);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(min.x + width, min.y + height);
    const ImVec4& fill = held ? EditorStyle::PRESSED : hovered ? EditorStyle::HOVERED : EditorStyle::RAISED;
    dl->AddRectFilled(min, max, ImGui::GetColorU32(fill), EditorStyle::px(4.0f));
    const ImU32 ink   = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 faint = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    const float side = ImGui::GetFontSize();
    const float midY = (min.y + max.y) * 0.5f;
    const float gap  = EditorStyle::px(6.0f);
    float x = min.x + EditorStyle::px(8.0f);
    const EditorIcon chevron = open ? EditorIcon::ChevronDown : EditorIcon::ChevronRight;
    drawEditorIcon(dl, chevron, ImVec2(x + side * 0.5f, midY), side * 0.32f, faint);
    x += side + gap;
    if (icon != EditorIcon::None) {
        drawEditorIcon(dl, icon, ImVec2(x + side * 0.5f, midY), side * 0.42f, ink);
        x += side + gap;
    }
    EditorStyle::pushHeadingFont();
    dl->AddText(ImVec2(x, midY - ImGui::GetTextLineHeight() * 0.5f), ink, title);
    ImGui::PopFont();

    if (menu) {
        const float button = height - EditorStyle::px(6.0f);
        ImGui::SameLine();
        const float inset = EditorStyle::px(3.0f);
        ImGui::SetCursorScreenPos(ImVec2(max.x - button - inset, min.y + inset));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        if (iconButton("cardMenu", EditorIcon::Ellipsis, false, true, "More", button)) {
            ImGui::OpenPopup(menu);
        }
        ImGui::PopStyleColor();
    }
    return open;
}
} // namespace

bool beginComponentCard(const char* title, EditorIcon icon, bool defaultOpen, const char* menu) {
    // Reset per frame, as ImGui's stacks are, so a skipped end cannot shift later cards.
    std::vector<CardState>& stack = cardStack();
    if (!stack.empty() && stack.back().frame != ImGui::GetFrameCount()) stack.clear();

    ImGui::PushID(title);
    ImGui::Spacing();

    const bool open = cardHeader(title, icon, defaultOpen, menu);

    CardState st;
    st.open  = open;
    st.frame = ImGui::GetFrameCount();
    if (open) {
        ImGui::Indent(cardIndent());
        ImGui::Spacing();
    }
    stack.push_back(st);
    return open;
}

void endComponentCard() {
    std::vector<CardState>& stack = cardStack();
    // No matching begin, so no PushID to pop.
    if (stack.empty()) return;

    const CardState st = stack.back();
    stack.pop_back();

    if (st.open) {
        ImGui::Spacing();
        ImGui::Unindent(cardIndent());
    }
    ImGui::PopID();
    ImGui::Spacing();
}

namespace {

/// Width of a line keeping [0, head) and [tail, len) with the ellipsis between.
float keptWidth(const char* s, size_t head, size_t tail, size_t len) {
    return ImGui::CalcTextSize(s, s + head).x + ImGui::CalcTextSize(s + tail, s + len).x;
}

} // namespace

std::string elidedLine(const char* text, float maxWidth) {
    const char* str = (text && text[0]) ? text : "(unnamed)";
    if (ImGui::CalcTextSize(str).x <= maxWidth) return str;

    const size_t len    = std::strlen(str);
    const float  budget = maxWidth - ImGui::CalcTextSize("...").x;

    // Alternate ends, so the halves stay equal in characters; never cut inside a
    // UTF-8 sequence, which renders a box.
    const std::string_view view(str, len);
    size_t head = 0;
    size_t tail = len;
    for (;;) {
        size_t grownHead = head;
        Utf8::next(view, grownHead);
        if (grownHead >= tail || keptWidth(str, grownHead, tail, len) > budget) break;
        head = grownHead;

        const size_t grownTail = Utf8::previous(view, tail);
        if (grownTail <= head || keptWidth(str, head, grownTail, len) > budget) break;
        tail = grownTail;
    }
    return std::string(str, head) + "..." + std::string(str + tail);
}

void emptyStateHeading(EditorIcon icon, const char* headline, const char* detail, int actionRows) {
    const ImVec2 region   = ImGui::GetContentRegionAvail();
    const float glyphSize = EditorStyle::px(56.0f);
    const float lineH     = ImGui::GetTextLineHeightWithSpacing();
    const float blockH    = glyphSize + lineH * 2.0f
        + ImGui::GetFrameHeight() * static_cast<float>(actionRows)
        + EditorStyle::px(24.0f);
    ImGui::Dummy(ImVec2(0.0f, std::max(0.0f, (region.y - blockH) * 0.35f)));

    const ImVec2 cur = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(0.0f, glyphSize));
    drawEditorIcon(
        ImGui::GetWindowDrawList(),
        icon,
        ImVec2(cur.x + region.x * 0.5f, cur.y + glyphSize * 0.5f),
        glyphSize * 0.40f,
        ImGui::GetColorU32(ImGuiCol_TextDisabled)
    );

    ImGui::Spacing();
    EditorStyle::pushHeadingFont();
    centerNextItem(ImGui::CalcTextSize(headline).x);
    ImGui::TextUnformatted(headline);
    ImGui::PopFont();
    // Wrapped, not clipped, when the panel is narrower than the line.
    const float detailW = ImGui::CalcTextSize(detail).x;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (detailW <= region.x) {
        centerNextItem(detailW);
        ImGui::TextUnformatted(detail);
    } else {
        ImGui::TextWrapped("%s", detail);
    }
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void centerNextItem(float width) {
    ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
}

bool removeButton(const char* id, const char* tooltip) {
    const float side = ImGui::GetFrameHeight();
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - side);
    // Neutral until the pointer is on it: a red cross on every row would be the loudest thing.
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool hot  = ImGui::IsMouseHoveringRect(at, ImVec2(at.x + side, at.y + side));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    const ImVec4 ink = hot ? EditorStyle::DANGER : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    ImGui::PushStyleColor(ImGuiCol_Text, ink);
    const bool pressed = iconButton(id, EditorIcon::Cross, false, true, tooltip, side);
    ImGui::PopStyleColor(2);
    return pressed;
}

void clippedLine(const char* text, float maxWidth, bool dim) {
    const std::string line = elidedLine(text, maxWidth);
    if (dim) ImGui::TextDisabled("%s", line.c_str());
    else     ImGui::TextUnformatted(line.c_str());
}

bool segmentedControl(const char* id, const Segment* segments, int count, int* chosen) {
    ImGui::PushID(id);
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float height  = ImGui::GetFrameHeight();
    const float padding = style.FramePadding.x * 1.5f;

    const auto widthOf = [&](int i) {
        return ImGui::CalcTextSize(segments[i].label, nullptr, true).x + padding * 2.0f;
    };
    float width = 0.0f;
    for (int i = 0; i < count; ++i) width += widthOf(i);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rounding = style.FrameRounding;
    const ImU32 well = ImGui::GetColorU32(ImGuiCol_FrameBg);
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), well, rounding);

    bool changed = false;
    float x = origin.x;
    for (int i = 0; i < count; ++i) {
        const float w = widthOf(i);
        const ImVec2 min(x, origin.y);
        const ImVec2 max(x + w, origin.y + height);
        ImGui::SetCursorScreenPos(min);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##segment", ImVec2(w, height)) && *chosen != i) {
            *chosen = i;
            changed = true;
        }
        ImGui::PopID();
        if (segments[i].tooltip) ImGui::SetItemTooltip("%s", segments[i].tooltip);
        const bool picked  = *chosen == i;
        const bool hovered = ImGui::IsItemHovered();
        if (picked || hovered) {
            // Inset, so the fill reads as a key pressed into the control.
            const ImU32 fill  = ImGui::GetColorU32(picked ? ImGuiCol_Header : ImGuiCol_FrameBgHovered);
            const float inset = EditorStyle::px(2.0f);
            const ImVec2 from(min.x + inset, min.y + inset);
            dl->AddRectFilled(from, ImVec2(max.x - inset, max.y - inset), fill, rounding - inset * 0.5f);
        }
        const ImVec4 text = segments[i].tint ? *segments[i].tint : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        const char*  end  = ImGui::FindRenderedTextEnd(segments[i].label);
        const ImVec2 size = ImGui::CalcTextSize(segments[i].label, end);
        const ImVec2 at(min.x + (w - size.x) * 0.5f, min.y + (height - size.y) * 0.5f);
        // Chosen, it reads on the accent fill as any pressed button's label does.
        const ImVec4 shown = picked ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : text;
        dl->AddText(at, ImGui::ColorConvertFloat4ToU32(shown), segments[i].label, end);
        x += w;
    }
    // Leave the cursor after the control, as an item of its size would.
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, height));
    ImGui::PopID();
    return changed;
}

bool toggleChip(const char* label, bool* on, const ImVec4& color) {
    ImVec4 off = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    off.w *= 0.75f;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * 0.5f);
    const auto tint = [&](float alpha) { return ImVec4(color.x, color.y, color.z, alpha); };
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, *on ? 1.0f : 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, tint(0.55f));
    ImGui::PushStyleColor(ImGuiCol_Button, tint(*on ? 0.22f : 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tint(0.30f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, tint(0.38f));
    ImGui::PushStyleColor(ImGuiCol_Text, *on ? color : off);
    const bool clicked = ImGui::Button(label);
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(2);
    if (clicked) *on = !*on;
    return clicked;
}

bool matchesFilter(const char* text, const char* filter) {
    if (!filter || !filter[0]) return true;
    for (const char* p = text; *p; ++p) {
        const char* s = filter;
        const char* t = p;
        while (*s && *t
            && tolower(static_cast<unsigned char>(*s)) == tolower(static_cast<unsigned char>(*t))) {
            ++s;
            ++t;
        }
        if (!*s) return true;
    }
    return false;
}

void inlineIcon(EditorIcon icon, float size, ImU32 color) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    drawEditorIcon(
        ImGui::GetWindowDrawList(),
        icon,
        ImVec2(p.x + size * 0.5f, p.y + size * 0.5f),
        size * 0.40f,
        color
    );
}

void cardNote(Note kind, const char* format, ...) {
    const ImVec4 color = kind == Note::Danger ? EditorStyle::DANGER
                           : kind == Note::Warning ? EditorStyle::WARNING
                           : EditorStyle::TEXT_SECONDARY;
    const EditorIcon icon = kind == Note::Danger ? EditorIcon::Error
                           : kind == Note::Warning ? EditorIcon::Warning
                           : EditorIcon::Info;
    const float side = ImGui::GetTextLineHeight();
    inlineIcon(icon, side, ImGui::GetColorU32(color));
    ImGui::SameLine(0.0f, EditorStyle::px(4.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushTextWrapPos(0.0f);
    va_list args;
    va_start(args, format);
    ImGui::TextV(format, args);
    va_end(args);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

int searchComboRows(const std::vector<ComboRow>& rows) {
    // One list is open at a time, so one search serves them all; each opening starts it empty.
    static char s_search[64] = {};
    const bool opening = ImGui::IsWindowAppearing();
    if (opening) s_search[0] = '\0';
    if (rows.size() > SEARCH_FROM) {
        if (opening) ImGui::SetKeyboardFocusHere();
        popupSearchField("##search", s_search, sizeof(s_search));
    }
    const bool narrowed = s_search[0] != '\0';
    std::vector<int> shown;
    shown.reserve(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        if (matchesFilter(rows[i].label.c_str(), s_search)) shown.push_back(static_cast<int>(i));
    }

    int picked = -1;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(shown.size()));
    while (clipper.Step()) {
        for (int k = clipper.DisplayStart; k < clipper.DisplayEnd; ++k) {
            const ComboRow& row = rows[static_cast<size_t>(shown[k])];
            const float step    = EditorStyle::px(12.0f);
            const float indent  = narrowed ? 0.0f : static_cast<float>(row.indent) * step;
            ImGui::PushID(shown[k]);
            if (indent > 0.0f) ImGui::Indent(indent);
            if (row.dim) ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::TEXT_SECONDARY);
            if (ImGui::Selectable(row.label.c_str(), row.selected)) picked = shown[k];
            if (row.dim) ImGui::PopStyleColor();
            if (row.tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", row.tip);
            if (indent > 0.0f) ImGui::Unindent(indent);
            ImGui::PopID();
        }
    }
    return picked;
}

bool foldRow(const char* label, bool& open) {
    const char*  shown  = ImGui::FindRenderedTextEnd(label);
    const ImVec2 at     = ImGui::GetCursorScreenPos();
    const float  height = ImGui::GetFrameHeight();
    const float  side   = ImGui::GetFontSize();
    const float  gap    = EditorStyle::px(4.0f);
    const float  text   = ImGui::CalcTextSize(label, shown).x;
    const float  width  = side + gap + text + ImGui::GetStyle().FramePadding.x;
    ImGui::PushID(label);
    if (ImGui::InvisibleButton("##fold", ImVec2(width, height))) open = !open;
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float midY = at.y + height * 0.5f;
    const ImU32 ink = ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    const EditorIcon chevron = open ? EditorIcon::ChevronDown : EditorIcon::ChevronRight;
    drawEditorIcon(dl, chevron, ImVec2(at.x + side * 0.5f, midY), side * 0.32f, ink);
    const ImVec2 textAt(at.x + side + gap, midY - ImGui::GetTextLineHeight() * 0.5f);
    dl->AddText(textAt, ImGui::GetColorU32(ImGuiCol_Text), label, shown);
    return open;
}

bool beginFold(const char* label, bool defaultOpen) {
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID id = ImGui::GetID(label);
    const bool was = storage->GetBool(id, defaultOpen);
    bool open = was;
    if (foldRow(label, open) != was) storage->SetBool(id, open);
    if (!open) return false;
    ImGui::PushID(label);
    ImGui::Indent(cardIndent());
    return true;
}

void endFold() {
    ImGui::Unindent(cardIndent());
    ImGui::PopID();
}

ListRowResult listRow(
    const char* id,
    EditorIcon icon,
    const char* label,
    bool selected,
    int depth,
    bool hasChildren,
    bool open,
    bool linked
) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight();
    const float side   = ImGui::GetFontSize();
    const float gap    = EditorStyle::px(4.0f);
    const float left   = style.FramePadding.x + static_cast<float>(depth) * style.IndentSpacing;
    const float textX  = left + (side + gap) * 2.0f;

    // The row spans the window to the pointer; its own size is its content, so a popup fits it.
    ImGui::PushID(id);
    if (selected) ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_Header));
    const ImVec2 size(textX + ImGui::CalcTextSize(label).x + style.FramePadding.x, height);
    // Overlap allowed: a row's own buttons (the Hierarchy's eye and lock) sit on it.
    const ImGuiSelectableFlags flags = ImGuiSelectableFlags_SpanAvailWidth
        | ImGuiSelectableFlags_AllowOverlap;
    const bool clicked = ImGui::Selectable("##row", selected, flags, size);
    if (selected) ImGui::PopStyleColor();
    ImGui::PopID();

    ListRowResult result;
    const float chevronMin = at.x + left;
    const float pointerX   = ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left].x;
    const bool  onChevron  = hasChildren && pointerX >= chevronMin && pointerX < chevronMin + side + gap;
    result.toggled = clicked && onChevron;
    result.clicked = clicked && !onChevron;

    ImDrawList* dl   = ImGui::GetWindowDrawList();
    const float midY = at.y + height * 0.5f;
    if (hasChildren) {
        const EditorIcon chevron = open ? EditorIcon::ChevronDown : EditorIcon::ChevronRight;
        const ImVec2     center(chevronMin + side * 0.5f, midY);
        drawEditorIcon(dl, chevron, center, side * 0.32f, ImGui::GetColorU32(ImGuiCol_TextDisabled));
    }
    const ImVec2 iconCenter(at.x + left + side + gap + side * 0.5f, midY);
    const ImU32 ink     = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 iconInk = linked ? ImGui::GetColorU32(EditorStyle::ACCENT) : ink;
    drawEditorIcon(dl, icon, iconCenter, side * 0.42f, iconInk);
    dl->AddText(ImVec2(at.x + textX, midY - ImGui::GetTextLineHeight() * 0.5f), ink, label);
    return result;
}

bool drawEngineMark(EditorRenderHooks& hooks, float side) {
    const GpuTextureId mark = hooks.chromeImage(
        (Roots::engineAssets() / "logo" / "vkm_engine_mark.png").string()
    );
    if (!mark) return false;
    // The UVs flip because the decode is bottom-up and ImGui's are not.
    ImGui::Image(imTexture(mark), ImVec2(side, side), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    return true;
}

} // namespace Vkm::Engine
