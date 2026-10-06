#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"
#include "core/reflect.h"
#include "system/render/editor_render_hooks.h"
#include "ui/editor_style.h"
#include "ui/editor_icons.h"

namespace Vkm::Engine {

class Scene;
enum class LightType;

/**
 * @brief XYZ vector control: three drag floats with colored reset buttons (X/Y/Z).
 *
 * Bounds hold on typed input too (PROP_CLAMP); lo == hi == 0 leaves an axis unbounded.
 *
 * @param label Row label.
 * @param values The three floats edited.
 * @param resetValue What an axis button restores.
 * @param speed Drag speed per axis.
 * @param lo Lower bound.
 * @param hi Upper bound.
 * @return True the frame any axis is edited.
 */
bool drawVec3Control(
    const char* label,
    float* values,
    float resetValue = 0.0f,
    float speed = 0.1f,
    float lo = 0.0f,
    float hi = 0.0f
);

/**
 * @brief Draw a right-aligned property label with a consistent column width.
 *
 * Sets the next item to full width so the paired widget fills the remainder.
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
 * @brief The bounds a prop row passes are a constraint, not a hint.
 *
 * Clamps Ctrl+click typed input too. Not AlwaysClamp, which would also clamp the
 * lo == hi == 0 range that means "unbounded".
 */
inline constexpr ImGuiSliderFlags PROP_CLAMP = ImGuiSliderFlags_ClampOnInput;

/**
 * @brief A slider that shows its value as a filled track.
 *
 * The value is written over the whole field, where ImGui's grab would cover it. Draw an
 * editor slider through this, so they all read the same. Ctrl+click is ImGui's own text field.
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
 * @brief Begin a combo drawn as one flat field.
 *
 * The arrow sits on the field's colour, not a button block. Begin an editor combo here,
 * and end it with ImGui::EndCombo when this returns true.
 *
 * @param id      ImGui id.
 * @param preview What the closed combo shows.
 * @param flags   ImGui's combo flags.
 * @return Whether the popup is open.
 */
bool beginCombo(const char* id, const char* preview, ImGuiComboFlags flags = 0);

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

inline bool propDragInt(
    const char* label,
    int* v,
    float speed,
    int lo,
    int hi,
    const char* tooltip = nullptr
) {
    return propRow(label, tooltip, [&] { return ImGui::DragInt("##v", v, speed, lo, hi, "%d", PROP_CLAMP); });
}

inline bool propDrag3(
    const char* label,
    float* v,
    float speed,
    float lo,
    float hi,
    const char* fmt = "%.3f",
    const char* tooltip = nullptr
) {
    return propRow(label, tooltip, [&] {
        return ImGui::DragFloat3("##v", v, speed, lo, hi, fmt, PROP_CLAMP);
    });
}

inline bool propColor3(
    const char* label,
    float* v,
    ImGuiColorEditFlags flags = ImGuiColorEditFlags_Float,
    const char* tooltip = nullptr
) {
    return propRow(label, tooltip, [&] { return ImGui::ColorEdit3("##v", v, flags); });
}

inline bool propColor4(
    const char* label,
    float* v,
    ImGuiColorEditFlags flags = ImGuiColorEditFlags_Float,
    const char* tooltip = nullptr
) {
    return propRow(label, tooltip, [&] { return ImGui::ColorEdit4("##v", v, flags); });
}

inline bool propCheckbox(const char* label, bool* v, const char* tooltip = nullptr) {
    return propRow(label, tooltip, [&] { return ImGui::Checkbox("##v", v); });
}

/**
 * @brief A labelled combo that edits an index rather than a value.
 *
 * For entries that set more than one field, which propValueCombo cannot express.
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
 * @brief Property row backed by a Combo over a fixed table of raw values.
 *
 * A stored value missing from the table previews as "?" until edited.
 *
 * @tparam N      Entries in both arrays.
 * @param label   Row label and id scope.
 * @param labels  Display string per value.
 * @param values  The value each label maps to.
 * @param v       The edited value.
 * @param tooltip Hover tooltip, or null.
 * @return Whether the value changed this frame.
 */
template <size_t N>
bool propValueCombo(
    const char* label,
    const char* const (&labels)[N],
    const uint32_t (&values)[N],
    uint32_t* v,
    const char* tooltip = nullptr
) {
    const int count = static_cast<int>(N);
    int idx = -1;
    for (int i = 0; i < count; ++i)
        if (values[i] == *v) idx = i;

    if (!propIndexCombo(label, labels, count, &idx, tooltip)) return false;
    *v = values[idx];
    return true;
}

/// Property row: an integer drag staged over a uint32_t member.
inline bool propDragU32(
    const char* label,
    uint32_t* v,
    float speed,
    uint32_t lo,
    uint32_t hi,
    const char* tooltip = nullptr
) {
    int staged = static_cast<int>(*v);
    const bool changed = propDragInt(
        label,
        &staged,
        speed,
        static_cast<int>(lo),
        static_cast<int>(hi),
        tooltip
    );
    if (changed) *v = static_cast<uint32_t>(staged < 0 ? 0 : staged);
    return changed;
}

/// Property row: a degrees drag staged over a radians-stored member.
inline bool propAngleDrag(
    const char* label,
    float* radians,
    float speed,
    float loDeg,
    float hiDeg,
    const char* tooltip = nullptr
) {
    float deg = glm::degrees(*radians);
    const bool changed = propDrag(label, &deg, speed, loDeg, hiDeg, "%.1f deg", tooltip);
    if (changed) *radians = glm::radians(deg);
    return changed;
}

/// Property row: a degrees slider staged over a radians-stored member.
inline bool propAngleSlider(
    const char* label,
    float* radians,
    float loDeg,
    float hiDeg,
    const char* tooltip = nullptr
) {
    float deg = glm::degrees(*radians);
    const bool changed = propSlider(label, &deg, loDeg, hiDeg, "%.0f deg", tooltip);
    if (changed) *radians = glm::radians(deg);
    return changed;
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
 * @brief Full-width Rebake button: bumps @p bakeVersion when pressed.
 *
 * @param bakeVersion The counter a bake compares against.
 * @return Whether it was pressed this frame.
 */
inline bool rebakeButton(uint32_t& bakeVersion) {
    if (ImGui::Button("Rebake", ImVec2(-1, 0))) {
        ++bakeVersion;
        return true;
    }
    return false;
}

/**
 * @brief Section heading inside a panel or popup.
 *
 * Distinct from TextDisabled, which is for hints and metadata.
 *
 * @param text Drawn in EditorStyle::HEADER_TEXT.
 */
inline void sectionLabel(const char* text) {
    ImGui::TextColored(EditorStyle::HEADER_TEXT, "%s", text);
}

/**
 * @brief The top of a panel's empty state: a glyph, what is missing, and the way out.
 *
 * Placed a third of the way down, allowing for @p actionRows buttons the caller then
 * places with centreNextItem.
 *
 * @param icon Glyph naming what the panel would show.
 * @param headline What is missing, e.g. "No entity selected".
 * @param detail How to get one.
 * @param actionRows How many rows of buttons follow.
 */
void emptyStateHeading(EditorIcon icon, const char* headline, const char* detail, int actionRows);

/**
 * @brief Put the next item of @p width at the centre of the row.
 *
 * At the left edge when wider than the row, so the tail clips, not the head.
 *
 * @param width The next item's width.
 */
void centreNextItem(float width);

/**
 * @brief The square face of an asset tile: its picture, or its glyph on a same-size square.
 *
 * Unframed, so tiles with and without a picture stand level (ImageButton insets by
 * FramePadding). Hover shows as an accent border, as a tint is invisible over a
 * picture. The face stays the last item and allows overlap, so later controls over it
 * are reachable.
 *
 * @param picture 0 shows @p glyph instead.
 * @param face Edge length, pixels.
 * @param accent The kind's colour, for the hover border.
 * @param glyph Drawn centred on a face with no picture.
 * @param glyphColor A faint one says a picture is still coming.
 * @param[out] faceMin Top-left corner of the face.
 * @param[out] faceMax Bottom-right corner of the face.
 * @return Whether the face was clicked.
 */
bool tileFace(
    ImTextureID picture,
    float face,
    const ImVec4& accent,
    EditorIcon glyph,
    const ImVec4& glyphColor,
    ImVec2& faceMin,
    ImVec2& faceMax
);

/**
 * @brief The accent strip down a tile's left edge.
 *
 * Call after the tile's group, so it lies over the face's edge, not under it.
 *
 * @param faceMin Top-left corner of the face.
 * @param faceMax Bottom-right corner of the face.
 * @param color The kind's colour, faded when the asset is unused.
 */
void tileStrip(ImVec2 faceMin, ImVec2 faceMax, const ImVec4& color);

/**
 * @brief Draw one line of text clipped to a width, ellipsised in the middle.
 *
 * Mid-line, because these lines differ at their ends: a tail cut leaves every tile
 * reading "assets/audio/to...".
 *
 * @param text Empty draws the "(unnamed)" placeholder.
 * @param maxWidth Pixels.
 * @param dim Draw in the disabled colour.
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

/**
 * @brief Test whether a string contains a filter substring, case-insensitively.
 *
 * @param text Candidate string.
 * @param filter Needle; empty matches everything.
 * @return True when filter occurs in text ignoring case, or is empty.
 */
bool matchesFilter(const char* text, const char* filter);

/**
 * @brief The search box a type-to-narrow popup opens with.
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
 * @brief Begin one of the strips that float over the viewport.
 *
 * End with endOverlayStrip whatever this returns.
 *
 * @param id    ImGui id.
 * @param size  Outer size; 0 on an axis fits the contents.
 * @return Whether its contents are drawn.
 */
bool beginOverlayStrip(const char* id, ImVec2 size);

/**
 * @brief End a strip beginOverlayStrip began.
 */
void endOverlayStrip();

/**
 * @brief A MenuItem with a leading entity/tool glyph.
 *
 * @param icon     Glyph ahead of the label.
 * @param label    Menu item text.
 * @param shortcut Right-aligned shortcut label, or null.
 * @param enabled  MenuItem's enabled flag.
 * @return True on the frame the item is activated.
 */
bool iconMenuItem(EditorIcon icon, const char* label, const char* shortcut = nullptr, bool enabled = true);

/**
 * @brief A component "card": a framed, accent-colored collapsible block.
 *
 * Always pair with endComponentCard(); remove the component only after it.
 *
 * @param title         Header text and id.
 * @param accent        One of EditorStyle::Accent.
 * @param defaultOpen   Whether the card starts expanded.
 * @param removeClicked Set true the frame the header's "x" is pressed; null draws no "x".
 * @return True when the body is expanded and should be drawn.
 */
bool beginComponentCard(
    const char* title,
    const ImVec4& accent,
    bool defaultOpen,
    bool* removeClicked = nullptr
);
void endComponentCard();

/**
 * @brief What a clip transport row did this frame.
 */
struct ClipTransport {
    bool rewound  = false;   ///< Stop pressed: at zero, not playing.
    bool authored = false;   ///< Loop or Speed changed; the scene saves these.
};

/**
 * @brief A clip player's transport row: Play/Pause, Stop, Loop and Speed.
 *
 * Play, Pause and Stop are previews, not edits; Loop and Speed are authored and reported
 * for the caller to record. Leaves the cursor on the row for the caller's controls.
 *
 * @tparam Player Animation or Animator.
 * @param id Prefix unique among the transports in one window.
 * @param player The player the row drives.
 * @param minSpeed The drag's lowest speed: 0 where the player cannot run backwards.
 * @param speedWidth Width of the speed drag; -1 fills the row.
 * @return What was pressed or changed.
 */
template <typename Player>
ClipTransport clipTransport(const char* id, Player& player, float minSpeed, float speedWidth) {
    ClipTransport result;
    const float gap    = EditorStyle::px(8.0f);
    const float height = ImGui::GetFrameHeight();

    ImGui::PushID(id);
    const EditorIcon playIcon = player.playing ? EditorIcon::Pause : EditorIcon::Play;
    const char*      playTip  = player.playing ? "Pause" : "Play";
    if (iconButton("play", playIcon, player.playing, true, playTip, height)) {
        player.playing = !player.playing;
    }
    ImGui::SameLine(0, gap);
    if (iconButton("stop", EditorIcon::Stop, false, true, "Stop (rewind to start)", height)) {
        player.playing = false;
        player.time    = 0.0f;
        result.rewound = true;
    }
    ImGui::SameLine(0, gap);
    const char* loopTip = player.looping ? "Looping" : "Play once";
    if (iconButton("loop", EditorIcon::Loop, player.looping, true, loopTip, height)) {
        player.looping  = !player.looping;
        result.authored = true;
    }
    ImGui::SameLine(0, gap);
    ImGui::SetNextItemWidth(speedWidth);
    result.authored |= ImGui::DragFloat(
        "##speed",
        &player.speed,
        0.005f,
        minSpeed,
        10.0f,
        "Speed %.2fx",
        PROP_CLAMP
    );
    ImGui::PopID();
    return result;
}

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
 * @brief Property row wrapping drawEnumCombo.
 *
 * @tparam E The enum.
 * @param label The row's label.
 * @param value Written on a pick.
 * @param tooltip Shown on hover, or null.
 * @return Whether a pick changed it this frame.
 */
template <typename E>
bool propEnumCombo(const char* label, E& value, const char* tooltip = nullptr) {
    return propRow(label, tooltip, [&] { return drawEnumCombo("##v", value); });
}

/**
 * @brief Stable Euler-angle edit cache for quaternion-backed rotations.
 *
 * Quaternion -> Euler is many-to-one and singular at +/-90 deg, so re-deriving it each
 * frame snaps typed axes and jitters. The edited Euler stays the truth, reseeded only
 * when the quaternion changes from outside. Call sync() before drawing, then:
 * `if (drawVec3Control(..., cache.degrees(), ...)) q = cache.toQuat();`
 *
 * @tparam Key Identifies the rotation source (an EntityId, a keyframe index); a new key
 *         reseeds.
 */
template<class Key>
class EulerCache {
    public:
        EulerCache() = default;
        ~EulerCache() = default;

        EulerCache(const EulerCache& other) = default;
        EulerCache& operator=(const EulerCache& other) = default;

        EulerCache(EulerCache && other) = default;
        EulerCache& operator=(EulerCache && other) = default;

    public:
        /**
         * @brief Reseed from @p q if the key changed or @p q diverged from the cache.
         *
         * @param key This frame's rotation source.
         * @param q   The stored rotation.
         */
        void sync(const Key& key, const glm::quat& q) {
            const glm::quat cached = glm::quat(glm::radians(m_degrees));
            const bool keyChanged   = !m_haveKey || !(m_key == key);
            const bool quatDiverged = glm::abs(glm::dot(cached, q)) < 0.9999f;
            if (keyChanged || quatDiverged) {
                m_degrees = glm::degrees(glm::eulerAngles(q));
                m_key     = key;
                m_haveKey = true;
            }
        }

        float* degrees() { return &m_degrees.x; }
        glm::quat toQuat() const { return glm::normalize(glm::quat(glm::radians(m_degrees))); }

    private:
        glm::vec3 m_degrees{0.0f};
        Key       m_key{};
        bool      m_haveKey = false;
};

/**
 * @brief Write the user-visible name of @p id into @p buf.
 *
 * Falls back to kind and slot ("Camera 7") without a non-empty Name.
 *
 * @param scene   The scene @p id lives in.
 * @param id      Entity named; it need not carry a Name.
 * @param buf     Receives the name, truncated.
 * @param bufSize Capacity of @p buf.
 */
void getEntityDisplayName(const Scene& scene, EntityId id, char* buf, size_t bufSize);

/**
 * @brief Which entity-type glyph represents @p id.
 *
 * Shares getEntityDisplayName's fallback ladder, so glyph and label agree.
 *
 * @param scene The scene @p id lives in.
 * @param id Its components choose the glyph.
 * @return The glyph for its kind.
 */
EditorIcon entityIconKind(const Scene& scene, EntityId id);

/**
 * @brief The glyph for a light of @p type.
 *
 * entityIconKind's light rung on its own, so a light's row and marker agree.
 *
 * @param type The light's type.
 * @return Its glyph; the point light's for a value out of range.
 */
EditorIcon lightIcon(LightType type);

/**
 * @brief Draw a non-interactive icon centred in a @p size square at the cursor.
 *
 * @param icon  Glyph.
 * @param size  Square side, screen pixels.
 * @param color Glyph colour.
 */
void inlineIcon(EditorIcon icon, float size, ImU32 color);

/**
 * @brief A tree node row prefixed with a type glyph: <arrow> <icon> <name>.
 *
 * The caller handles click, drag and context menu.
 *
 * @param idPtr The node's id.
 * @param flags TreeNodeEx's flags.
 * @param icon Glyph between the arrow and the name.
 * @param name The label.
 * @return Whether the node is open.
 */
bool entityTreeNode(const void* idPtr, ImGuiTreeNodeFlags flags, EditorIcon icon, const char* name);

/**
 * @brief A Selectable row prefixed with a type glyph.
 *
 * @param idStr Keeps the ImGui id stable when names collide.
 * @param selected Whether the row draws as selected.
 * @param icon Glyph ahead of the name.
 * @param name The label.
 * @return True the frame it is clicked.
 */
bool entitySelectable(const char* idStr, bool selected, EditorIcon icon, const char* name);

/**
 * @brief Wrap a backend texture as the handle ImGui's image widgets take.
 *
 * @param id As EditorRenderHooks hands it out.
 * @return ImGui's opaque ImTextureID.
 */
inline ImTextureID imTexture(GpuTextureId id) {
    return static_cast<ImTextureID>(static_cast<intptr_t>(id));
}

} // namespace Vkm::Engine
