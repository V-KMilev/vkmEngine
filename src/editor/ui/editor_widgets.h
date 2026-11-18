#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"
#include "base/reflect/enum_names.h"
#include "base/math/curve.h"
#include "system/render/editor_render_hooks.h"
#include "ui/style/editor_style.h"
#include "ui/style/editor_icons.h"

namespace Vkm::Engine {

/**
 * @brief The three fields of a vector row, each marked by its axis's color: drawVec3Control less its
 *        label.
 *
 * @param values The three floats edited.
 * @param speed  Drag speed per axis.
 * @param lo     Lower bound.
 * @param hi     Upper bound.
 * @param format printf format for each.
 * @return True the frame any axis is edited.
 */
bool vec3Fields(float* values, float speed, float lo, float hi, const char* format);

/**
 * @brief Draw a right-aligned property label, and set the next item to fill the rest of the row.
 *
 * @param label Label column text.
 */
void drawPropertyLabel(const char* label);

/**
 * @brief A property row: right-aligned label, full-width control, optional tooltip.
 *
 * The id is scoped by @p label with a hidden "##v", so distinct labels never collide.
 *
 * @tparam Widget A callable taking nothing and returning bool.
 * @param label The row's label and id scope.
 * @param tooltip Shown on hover, or null.
 * @param widget Draws the control and returns whether it was edited.
 * @return What @p widget returned.
 */
template <typename Widget>
inline bool propRow(const char* label, const char* tooltip, Widget&& widget) {
    drawPropertyLabel(label);
    ImGui::PushID(label);
    const bool changed = widget();
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return changed;
}

/**
 * @brief A row that only says: @p label in the label column, @p value beside it.
 *
 * @param label What it is.
 * @param value What it says.
 */
void factRow(const char* label, const char* value);

inline constexpr ImGuiSliderFlags PROP_CLAMP = ImGuiSliderFlags_ClampOnInput;
    ///< A prop row's bounds clamp typed input too; AlwaysClamp would also clamp lo == hi == 0, "unbounded".

/**
 * @brief The editor's slider: a filled track, the value written over the whole field.
 *
 * Draw an editor slider through this, so they all read the same. Ctrl+click is ImGui's own text field.
 *
 * @param id     ImGui id; the label is hidden ("##...").
 * @param type   ImGuiDataType_Float or ImGuiDataType_S32.
 * @param v      The value, of @p type.
 * @param lo     Low end, of @p type.
 * @param hi     High end, of @p type.
 * @param format printf format for the value.
 * @param flags  ImGui's slider flags.
 * @return Whether the value changed this frame.
 */
bool sliderScalar(
    const char* id,
    ImGuiDataType type,
    void* v,
    const void* lo,
    const void* hi,
    const char* format,
    ImGuiSliderFlags flags
);

/**
 * @brief A float sliderScalar.
 *
 * @param id     Hidden-label ImGui id.
 * @param v      The value.
 * @param lo     Low end.
 * @param hi     High end.
 * @param format printf format for the value.
 * @param flags  ImGui's slider flags.
 * @return Whether the value changed this frame.
 */
inline bool sliderFloat(
    const char* id,
    float* v,
    float lo,
    float hi,
    const char* format = "%.3f",
    ImGuiSliderFlags flags = PROP_CLAMP
) {
    return sliderScalar(id, ImGuiDataType_Float, v, &lo, &hi, format, flags);
}

/**
 * @brief An int sliderScalar.
 *
 * @param id     Hidden-label ImGui id.
 * @param v      The value.
 * @param lo     Low end.
 * @param hi     High end.
 * @param format printf format for the value.
 * @param flags  ImGui's slider flags.
 * @return Whether the value changed this frame.
 */
inline bool sliderInt(
    const char* id,
    int* v,
    int lo,
    int hi,
    const char* format = "%d",
    ImGuiSliderFlags flags = PROP_CLAMP
) {
    return sliderScalar(id, ImGuiDataType_S32, v, &lo, &hi, format, flags);
}

/**
 * @brief Begin a combo drawn as one flat field, the editor's chevron at its right.
 *
 * End it with ImGui::EndCombo when this returns true.
 *
 * @param id      ImGui id.
 * @param preview What the closed combo shows, cut short of the chevron.
 * @param flags   ImGui's combo flags.
 * @return Whether the popup is open.
 */
bool beginCombo(const char* id, const char* preview, ImGuiComboFlags flags = 0);

/**
 * @brief A submenu inside a menu, its arrow the editor's chevron; closed with ImGui::EndMenu.
 *
 * @return Whether it is open.
 */
bool beginSubmenu(const char* label, bool enabled = true);

/**
 * @brief A combo over a list of labels, edited by index.
 *
 * @param id     ImGui id.
 * @param index  Written on a pick.
 * @param labels One label per entry.
 * @param count  Entries in @p labels.
 * @return Whether a pick changed the index this frame.
 */
bool comboList(const char* id, int* index, const char* const* labels, int count);

inline bool propSlider(
    const char* label,
    float* v,
    float lo,
    float hi,
    const char* fmt = "%.3f",
    const char* tooltip = nullptr
) {
    return propRow(label, tooltip, [&] { return sliderFloat("##v", v, lo, hi, fmt); });
}

inline bool propSliderInt(const char* label, int* v, int lo, int hi, const char* tooltip = nullptr) {
    return propRow(label, tooltip, [&] { return sliderInt("##v", v, lo, hi); });
}

inline bool propDrag(
    const char* label,
    float* v,
    float speed,
    float lo,
    float hi,
    const char* fmt = "%.3f",
    const char* tooltip = nullptr
) {
    return propRow(label, tooltip, [&] {
        return ImGui::DragFloat("##v", v, speed, lo, hi, fmt, PROP_CLAMP);
    });
}

inline bool propCheckbox(const char* label, bool* v, const char* tooltip = nullptr) {
    return propRow(label, tooltip, [&] { return ImGui::Checkbox("##v", v); });
}

/**
 * @brief A labeled combo that edits an index rather than a value.
 *
 * For entries that set more than one field, which valueCombo cannot express.
 *
 * @param label   Row label.
 * @param labels  Entry labels.
 * @param count   Entries in @p labels.
 * @param index   The edited index.
 * @param tooltip Hover tooltip, or null.
 * @return Whether the index changed this frame.
 */
inline bool propIndexCombo(
    const char* label,
    const char* const* labels,
    int count,
    int* index,
    const char* tooltip = nullptr
) {
    return propRow(label, tooltip, [&] { return comboList("##v", index, labels, count); });
}

/**
 * @brief A combo over a fixed table of raw values; a stored value missing from it previews as "?".
 *
 * @tparam N     Entries in both arrays.
 * @param id     ImGui id.
 * @param labels Display string per value.
 * @param values The value each label maps to.
 * @param v      The edited value.
 * @return Whether the value changed this frame.
 */
template <size_t N>
bool valueCombo(const char* id, const char* const (&labels)[N], const uint32_t (&values)[N], uint32_t* v) {
    const int count = static_cast<int>(N);
    int idx = -1;
    for (int i = 0; i < count; ++i) {
        if (values[i] == *v) idx = i;
    }
    if (!comboList(id, &idx, labels, count)) return false;
    *v = values[idx];
    return true;
}

/**
 * @brief Property row: a string edit written straight into the string.
 *
 * @param label The row's label.
 * @param s Edited in place.
 * @param tooltip Shown on hover, or null.
 * @return Whether the string changed this frame.
 */
inline bool propString(const char* label, std::string& s, const char* tooltip = nullptr) {
    return propRow(label, tooltip, [&] { return ImGui::InputText("##v", &s); });
}

/**
 * @brief A remove button at the right end of the current line: a cross, in danger's color only
 *        under the pointer.
 *
 * @param id      Unique among the line's items.
 * @param tooltip What it removes, on hover.
 * @return Whether it was pressed.
 */
bool removeButton(const char* id, const char* tooltip);

/**
 * @brief Section heading inside a panel or popup; TextDisabled is for hints and metadata.
 *
 * @param text The heading.
 */
inline void sectionLabel(const char* text) {
    EditorStyle::pushHeadingFont();
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

/**
 * @brief The top of a panel's empty state: a glyph, what is missing, and the way out.
 *
 * Placed a third of the way down, allowing for @p actionRows rows of buttons the caller then
 * places with centerNextItem.
 *
 * @param icon Glyph naming what the panel would show.
 * @param headline What is missing, e.g. "No entity selected".
 * @param detail How to get one.
 * @param actionRows How many rows of buttons follow.
 */
void emptyStateHeading(EditorIcon icon, const char* headline, const char* detail, int actionRows);

/**
 * @brief Put the next item of @p width at the center of the row.
 *
 * At the left edge when wider than the row, so the tail clips, not the head.
 *
 * @param width The next item's width.
 */
void centerNextItem(float width);

/**
 * @brief Draw one line of text cut to a width, elided in the middle.
 *
 * Mid-line, because these lines differ at their ends: a tail cut leaves every tile
 * reading "assets/audio/to...".
 *
 * @param text Empty draws the "(unnamed)" placeholder.
 * @param maxWidth Pixels.
 * @param dim Draw in the disabled color.
 */
void clippedLine(const char* text, float maxWidth, bool dim);

/**
 * @brief The same line, returned rather than drawn, for draw-list callers.
 *
 * @param text Empty yields the "(unnamed)" placeholder.
 * @param maxWidth Pixels.
 * @return The line, cut mid-way with "..." when it does not fit.
 */
std::string elidedLine(const char* text, float maxWidth);

inline constexpr size_t SEARCH_FROM = 8;  ///< Past this many rows a picker's list opens with a search,
                                          ///< focused.

/**
 * @brief One row of a searchCombo's list.
 */
struct ComboRow {
    std::string label;
    bool        selected = false;
    bool        dim      = false;    ///< In the secondary text: not loaded, not quite a choice.
    int         indent   = 0;        ///< Steps in, as a tree's depth; none while a search narrows it.
    const char* tip      = nullptr;
};

/**
 * @brief The list of an open combo, narrowed as the search above it is typed into.
 *
 * @param rows The rows, in order.
 * @return The index of the row picked, or -1.
 */
int searchComboRows(const std::vector<ComboRow>& rows);

/**
 * @brief A combo whose rows are asked for only while it is open, with a search past SEARCH_FROM.
 *
 * @param id      The combo's id.
 * @param preview What the closed combo shows.
 * @param rows    Called while the list is open; returns its rows.
 * @return The index of the row picked this frame, or -1.
 */
template <typename ListRows>
int searchCombo(const char* id, const char* preview, ListRows&& rows) {
    if (!beginCombo(id, preview)) return -1;
    const int picked = searchComboRows(rows());
    ImGui::EndCombo();
    return picked;
}

/**
 * @brief Whether @p text contains @p filter, ignoring case.
 *
 * @param text Candidate string.
 * @param filter Needle; empty matches everything.
 * @return True when filter occurs in text ignoring case, or is empty.
 */
bool matchesFilter(const char* text, const char* filter);

/**
 * @brief One choice of a segmented control.
 */
struct Segment {
    const char*   label;              ///< What it says; it is known by its place, not by this.
    const ImVec4* tint    = nullptr;  ///< Its text's color, or null for the usual one.
    const char*   tooltip = nullptr;  ///< Shown on hover, or null for none.
};

/**
 * @brief A row of choices drawn as one joined control, the chosen one filled.
 *
 * For a few views of one thing, where tabs would nest inside a tab.
 *
 * @param id       ImGui id of the control.
 * @param segments The choices, left to right.
 * @param count    How many there are.
 * @param chosen   Index of the chosen one; set when another is clicked.
 * @return Whether the choice changed.
 */
bool segmentedControl(const char* id, const Segment* segments, int count, int* chosen);

/**
 * @brief A pill that toggles: tinted by @p color while on, dim while off.
 *
 * For filters whose state is the point, where a checkbox's tick says less than the color.
 *
 * @param label Its text; "###" hides a changing part from the id.
 * @param on    Its state, flipped by a click.
 * @param color What it means: the tint of its fill, ring and text while on.
 * @return Whether it was clicked.
 */
bool toggleChip(const char* label, bool* on, const ImVec4& color);

/**
 * @brief A panel's search box: Escape empties it, and Ctrl+F reaches it while its panel has focus.
 *
 * @param id    ImGui id, unique within the panel.
 * @param buf   The needle.
 * @param size  Capacity of @p buf, including the terminator.
 * @param width Screen pixels; negative leaves that much room at the right edge.
 * @param hint  What it shows while empty.
 * @return Whether the needle changed.
 */
bool searchField(const char* id, char* buf, size_t size, float width, const char* hint = "Search...");

/**
 * @brief The search box a type-to-narrow popup opens with, focused as the popup appears.
 *
 * @param id    ImGui id fragment, unique within the popup ("##compFilter").
 * @param buf   The needle, emptied whenever the popup appears.
 * @param size  Capacity of @p buf, including the terminator.
 * @param width Screen pixels; negative reaches the right edge.
 */
inline void popupSearchField(const char* id, char* buf, size_t size, float width = -1.0f) {
    if (ImGui::IsWindowAppearing()) {
        buf[0] = '\0';
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(width);
    ImGui::InputTextWithHint(id, "Search...", buf, size, ImGuiInputTextFlags_EscapeClearsAll);
    ImGui::Separator();
}

/**
 * @brief What a card's note says of its component: a fact, a surprise, or that it does nothing.
 */
enum class Note { Info, Warning, Danger };

/**
 * @brief A card's line about its component: the kind's icon and wrapped text in its color, an Info
 *        in the secondary text.
 *
 * @param kind   What the note is.
 * @param format printf format of its one sentence.
 */
void cardNote(Note kind, const char* format, ...) IM_FMTARGS(2);

/**
 * @brief A component "card": a collapsible block under the editor's own header.
 *
 * The header is a chevron, the card's icon, its title and, given a menu, a "..." button that
 * opens it, as a right click on the header does. Always pair with endComponentCard().
 *
 * @param title       Header text and id.
 * @param icon        What the card is for; None draws no glyph.
 * @param defaultOpen Whether the card starts expanded.
 * @param menu        The popup the header opens, which the caller begins before endComponentCard;
 *                    null for none.
 * @return True when the body is expanded and should be drawn.
 */
bool beginComponentCard(const char* title, EditorIcon icon, bool defaultOpen, const char* menu = nullptr);
void endComponentCard();

/**
 * @brief Enum dropdown, one row per name, from the enum's VKM_ENUM_NAMES registration.
 *
 * Fills the row, pairing with drawPropertyLabel.
 *
 * @tparam E The enum.
 * @param id A unique ImGui id.
 * @param value Written on a pick.
 * @return Whether a pick changed it this frame.
 */
template <typename E>
bool drawEnumCombo(const char* id, E& value) {
    using Names = Reflect::EnumNames<E>;
    int idx = static_cast<int>(value);
    ImGui::SetNextItemWidth(-1);
    if (comboList(id, &idx, Names::values, static_cast<int>(Names::count))) {
        value = static_cast<E>(idx);
        return true;
    }
    return false;
}

/**
 * @brief Draw a non-interactive icon centered in a @p size square at the cursor.
 *
 * @param icon  Glyph.
 * @param size  Square side, screen pixels.
 * @param color Glyph color.
 */
void inlineIcon(EditorIcon icon, float size, ImU32 color);

/**
 * @brief A row folding what follows it: the editor's chevron and @p label. A click flips @p open.
 *
 * @param label Shown, and the row's id; text after "###" is the id alone.
 * @param open  Whether what follows is shown; the caller keeps it.
 * @return @p open, as the click left it.
 */
bool foldRow(const char* label, bool& open);

/**
 * @brief foldRow keeping its state in ImGui's storage, as a tree node does, and indenting what
 *        it opens. Pair an open one with endFold().
 *
 * @param label       As foldRow's; also the id scope of what it opens.
 * @param defaultOpen Whether it starts open.
 * @return Whether it is open.
 */
bool beginFold(const char* label, bool defaultOpen);
void endFold();

/**
 * @brief What a click on a list row did.
 */
struct ListRowResult {
    bool clicked = false;  ///< The row was clicked and let go of on it, not dragged off.
    bool toggled = false;  ///< Its chevron was clicked: open it, or close it.
};

/**
 * @brief One row of a list: a chevron when it has children, its icon and its label, indented.
 *
 * A Selectable spanning the row, so drag and drop, a context menu and hover act on it as on any
 * item; it is the last item when this returns. A click on the chevron toggles, not selects.
 *
 * @param id          Unique among the window's rows; not shown.
 * @param icon        What kind of thing the row is.
 * @param label       What it is called.
 * @param selected    Drawn in the selection's color.
 * @param depth       Steps of indent.
 * @param hasChildren Whether it draws a chevron.
 * @param open        Which way the chevron points.
 * @param linked      Draws the icon in the accent: the row is a prefab's.
 * @return What was clicked.
 */
ListRowResult listRow(
    const char* id,
    EditorIcon icon,
    const char* label,
    bool selected,
    int depth = 0,
    bool hasChildren = false,
    bool open = false,
    bool linked = false
);

/**
 * @brief Wrap a backend texture as the handle ImGui's image widgets take.
 *
 * @param id As EditorRenderHooks hands it out.
 * @return ImGui's opaque ImTextureID.
 */
inline ImTextureID imTexture(GpuTextureId id) {
    return static_cast<ImTextureID>(static_cast<intptr_t>(id));
}

/**
 * @brief Draw the engine's mark, @p side pixels square, through the render seam.
 *
 * Not a texture of the editor's own: the mark is the engine's, so it must not land in the
 * project's asset library.
 *
 * @param hooks Whose chromeImage() loads it.
 * @param side  Its edge, in pixels.
 * @return Whether it drew; false when the image could not be decoded.
 */
bool drawEngineMark(EditorRenderHooks& hooks, float side);

} // namespace Vkm::Engine
