#include "ui/value_widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

#include <glm/gtc/type_ptr.hpp>

#include "ui/style/editor_style.h"
#include <vector>

#include <imgui.h>

#include "ecs/scene.h"
#include "ecs/prefab_instances.h"
#include "ui/editor_widgets.h"
#include "ui/entity_labels.h"
#include <algorithm>
#include <array>

#include <imgui.h>
#include <imgui_internal.h>

#include "ui/style/editor_icons.h"

namespace Vkm::Engine {

namespace {

/// The smallest step a printf format shows: 0.01 for "%.2f"; 0.001 without a precision.
float shownStep(const char* format) {
    const char* dot  = format ? std::strchr(format, '.') : nullptr;
    const int places = dot ? std::atoi(dot + 1) : 3;
    return std::pow(10.0f, -static_cast<float>(places));
}

/**
 * @brief How far a drag moves a value a pixel: a hundredth of the value, never finer than the
 *        format shows, nor so coarse that the field's range is crossed in under a hundred pixels.
 */
float dragSpeed(const FieldInfo& info, float magnitude, const char* format) {
    float speed = std::max(std::abs(magnitude) * 0.01f, shownStep(format));
    if (info.max > info.min) speed = std::min(speed, (info.max - info.min) / 100.0f);
    return speed;
}

/// The field's range as ImGui takes it: unbounded when it has none.
float lowOf(const FieldInfo& info)  { return info.max > info.min ? info.min : 0.0f; }
float highOf(const FieldInfo& info) { return info.max > info.min ? info.max : 0.0f; }

/// The same in degrees, for an angle held in radians.
FieldInfo inDegrees(const FieldInfo& info) {
    FieldInfo degrees = info;
    degrees.min = glm::degrees(info.min);
    degrees.max = glm::degrees(info.max);
    return degrees;
}

bool editScalar(const FieldInfo& info, float& value, const char* format, bool slider) {
    ImGui::SetNextItemWidth(-1.0f);
    if (slider && info.max > info.min) return sliderFloat("##v", &value, info.min, info.max, format);
    const float speed = dragSpeed(info, value, format);
    return ImGui::DragFloat("##v", &value, speed, lowOf(info), highOf(info), format, PROP_CLAMP);
}

} // namespace

bool editFloat(const FieldInfo& info, float& value, const char* format, Shown shown) {
    const bool slider = shown == Shown::Slider;
    if (shown != Shown::Angle) return editScalar(info, value, format ? format : "%.2f", slider);
    float degrees = glm::degrees(value);
    if (!editScalar(inDegrees(info), degrees, format ? format : "%.1f deg", false)) return false;
    value = glm::radians(degrees);
    return true;
}

bool editInt(const FieldInfo& info, int& value, Shown shown) {
    const int lo = static_cast<int>(lowOf(info));
    const int hi = static_cast<int>(highOf(info));
    ImGui::SetNextItemWidth(-1.0f);
    if (shown == Shown::Slider && hi > lo) return sliderInt("##v", &value, lo, hi);
    const float speed = std::max(std::abs(static_cast<float>(value)) * 0.01f, 0.1f);
    return ImGui::DragInt("##v", &value, speed, lo, hi, "%d", PROP_CLAMP);
}

bool editU32(const FieldInfo& info, uint32_t& value) {
    // Staged as an int: ImGui's own unsigned drag wraps below zero.
    const int hi = info.max > info.min ? static_cast<int>(info.max) : std::numeric_limits<int>::max();
    const int lo = static_cast<int>(std::max(info.min, 0.0f));
    int staged = static_cast<int>(std::min<uint32_t>(value, static_cast<uint32_t>(hi)));
    const float speed = std::max(static_cast<float>(staged) * 0.01f, 0.1f);
    ImGui::SetNextItemWidth(-1.0f);
    if (!ImGui::DragInt("##v", &staged, speed, lo, hi, "%d", PROP_CLAMP)) return false;
    value = static_cast<uint32_t>(std::max(staged, lo));
    return true;
}

bool editVec2(const FieldInfo& info, glm::vec2& value, const char* format) {
    const char* shown = format ? format : "%.2f";
    const float speed = dragSpeed(info, std::max(std::abs(value.x), std::abs(value.y)), shown);
    ImGui::SetNextItemWidth(-1.0f);
    float* values = glm::value_ptr(value);
    return ImGui::DragFloat2("##v", values, speed, lowOf(info), highOf(info), shown, PROP_CLAMP);
}

bool editVec3(const FieldInfo& info, glm::vec3& value, const char* format, Shown shown) {
    if (shown == Shown::Color || namesAColor(info.name)) {
        ImGui::SetNextItemWidth(-1.0f);
        return ImGui::ColorEdit3("##v", glm::value_ptr(value), ImGuiColorEditFlags_Float);
    }
    const char* fmt   = format ? format : "%.2f";
    const float most  = std::max({std::abs(value.x), std::abs(value.y), std::abs(value.z)});
    const float speed = dragSpeed(info, most, fmt);
    return vec3Fields(glm::value_ptr(value), speed, lowOf(info), highOf(info), fmt);
}

bool editVec4(const FieldInfo& info, glm::vec4& value, const char* format, Shown shown) {
    ImGui::SetNextItemWidth(-1.0f);
    if (shown == Shown::Color || namesAColor(info.name)) {
        return ImGui::ColorEdit4("##v", glm::value_ptr(value), ImGuiColorEditFlags_Float);
    }
    const char* fmt   = format ? format : "%.2f";
    const float first = std::max(std::abs(value.x), std::abs(value.y));
    const float most  = std::max(first, std::max(std::abs(value.z), std::abs(value.w)));
    const float speed = dragSpeed(info, most, fmt);
    return ImGui::DragFloat4("##v", glm::value_ptr(value), speed, lowOf(info), highOf(info), fmt, PROP_CLAMP);
}

bool editRange(const FieldInfo& info, Math::FloatRange& value, const char* format, Shown shown) {
    const bool angle = shown == Shown::Angle;
    const FieldInfo bounds = angle ? inDegrees(info) : info;
    const Math::FloatRange degrees{glm::degrees(value.min), glm::degrees(value.max)};
    Math::FloatRange staged = angle ? degrees : value;
    const char* fmt = format ? format : angle ? "%.0f deg" : "%.2f";
    const float most  = std::max(std::abs(staged.min), std::abs(staged.max));
    const float speed = dragSpeed(bounds, most, fmt);
    ImGui::SetNextItemWidth(-1.0f);
    const float lo = lowOf(bounds);
    const float hi = highOf(bounds);
    if (!ImGui::DragFloatRange2("##v", &staged.min, &staged.max, speed, lo, hi, fmt, fmt, PROP_CLAMP)) {
        return false;
    }
    value = angle ? Math::FloatRange{glm::radians(staged.min), glm::radians(staged.max)} : staged;
    return true;
}

bool editCurve(const FieldInfo& info, Math::Curve& value) {
    return curveWidget(value, lowOf(info), info.max > info.min ? info.max : 1.0f, info.tip);
}

bool editGradient(const FieldInfo& info, Math::Gradient& value) {
    return gradientWidget(value, info.tip);
}

RowAsk rowLabel(const char* label, const char* tip, bool overridden) {
    if (overridden) ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::ACCENT);
    drawPropertyLabel(label);
    if (overridden) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        if (overridden && tip) ImGui::SetTooltip("%s\nOverridden by this instance", tip);
        else if (overridden)   ImGui::SetTooltip("Overridden by this instance");
        else if (tip)          ImGui::SetTooltip("%s", tip);
    }
    RowAsk asked = RowAsk::None;
    ImGui::PushID(label);
    if (ImGui::BeginPopupContextItem("##rowMenu")) {
        if (ImGui::MenuItem("Reset to Default")) asked = RowAsk::Reset;
        if (overridden && ImGui::MenuItem("Revert to Prefab")) asked = RowAsk::Revert;
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return asked;
}

bool drawVec3Control(
    const char* label,
    const char* tip,
    float* values,
    float resetValue,
    float speed,
    float lo,
    float hi
) {
    const RowAsk asked = rowLabel(label, tip, false);
    ImGui::PushID(label);
    bool changed = vec3Fields(values, speed, lo, hi, "%.2f");
    ImGui::PopID();
    if (asked == RowAsk::Reset) {
        for (int i = 0; i < 3; ++i) values[i] = resetValue;
        changed = true;
    }
    return changed;
}

bool propEntity(Scene& scene, EntityId self, const char* label, EntityId& target, const char* tip) {
    char preview[96] = "None";
    if (scene.isAlive(target)) getEntityDisplayName(scene, target, preview, sizeof(preview));
    const bool prefabOwned = static_cast<bool>(Prefab::instanceRootOf(scene, self));
    ImGui::BeginDisabled(prefabOwned);
    // Every entity, named or not; the scene is walked only while the list is open.
    std::vector<EntityId> listed;
    const bool changed = propRow(label, tip, [&] {
        const int picked = searchCombo("##v", preview, [&] {
            std::vector<ComboRow> rows;
            rows.push_back({"None", !scene.isAlive(target)});
            scene.forEachEntity([&](EntityId other) {
                if (other == self) return;
                char name[96];
                getEntityDisplayName(scene, other, name, sizeof(name));
                listed.push_back(other);
                rows.push_back({name, other == target});
            });
            return rows;
        });
        if (picked < 0) return false;
        const EntityId was = target;
        target = picked == 0 ? EntityId{} : listed[static_cast<size_t>(picked) - 1];
        return target != was;
    });
    ImGui::EndDisabled();
    if (prefabOwned) cardNote(Note::Info, "Set in the prefab: an override cannot name an entity.");
    return changed;
}

namespace {

constexpr int PREVIEW_SAMPLES = 48;  ///< Samples a curve's plot and a gradient's strip take across the span.

constexpr float PLOT_HEADROOM = 1.15f;  ///< How far a curve's plot reaches past its peak, so the peak does
                                        ///< not ride its frame.

/// Put @p keys back in time order, so a key dragged past another reads as it looks.
template <typename Key>
void sortByTime(std::vector<Key>& keys) {
    const auto earlier = [](const Key& a, const Key& b) { return a.time < b.time; };
    std::stable_sort(keys.begin(), keys.end(), earlier);
}

/**
 * @brief Where a new key goes: halfway across the widest gap between keys, or the span's ends.
 *
 * @tparam Key A key with a `time`.
 * @param keys The keys there are, in any order.
 * @return The new key's time.
 */
template <typename Key>
float newKeyTime(const std::vector<Key>& keys) {
    std::vector<float> times{0.0f, 1.0f};
    for (const Key& key : keys) times.push_back(std::clamp(key.time, 0.0f, 1.0f));
    std::sort(times.begin(), times.end());
    float at     = 0.5f;
    float widest = -1.0f;
    for (size_t i = 1; i < times.size(); ++i) {
        if (times[i] - times[i - 1] <= widest) continue;
        widest = times[i] - times[i - 1];
        at     = 0.5f * (times[i] + times[i - 1]);
    }
    return at;
}

/**
 * @brief Draw a row of controls per key of @p keys, each with a remove button, and an add button.
 *
 * @tparam Key  A key with a `time`.
 * @tparam Edit Callable taking a `Key&` and returning whether it changed it.
 * @tparam Make Callable taking a time and returning a Key.
 * @param keys    Keys the rows edit, left in time order.
 * @param edit    Draws one key's own controls beside its time and returns whether they changed it.
 * @param makeKey Makes the key the add button inserts at a time, holding what the keys read there.
 * @return Whether anything was edited, added or removed.
 */
template <typename Key, typename Edit, typename Make>
bool keyRows(std::vector<Key>& keys, Edit&& edit, Make&& makeKey) {
    bool changed  = false;
    bool timed    = false;
    bool dragging = false;
    const float timeWidth = EditorStyle::px(56.0f);
    const float remove    = ImGui::GetFrameHeight();
    for (size_t i = 0; i < keys.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(timeWidth);
        timed    |= ImGui::DragFloat("##time", &keys[i].time, 0.005f, 0.0f, 1.0f, "%.2f", PROP_CLAMP);
        dragging |= ImGui::IsItemActive();
        timed    |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where in the life, 0 at birth to 1 at death");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - remove - ImGui::GetStyle().ItemSpacing.x);
        changed |= edit(keys[i]);
        ImGui::SameLine();
        const bool removed = iconButton("remove", EditorIcon::Cross, false, true, "Remove this key", remove);
        ImGui::PopID();
        if (removed) {
            keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(i));
            changed = true;
            break;
        }
    }
    if (ImGui::Button("Add Key")) {
        keys.push_back(makeKey(newKeyTime(keys)));
        timed = true;
    }
    // Not mid-drag: rows go by index, so a key reordered under the mouse would hand the drag on.
    // Letting go of a time is no edit itself, so the release frame sorts too.
    if (timed && !dragging) sortByTime(keys);
    return changed || timed;
}

/// Shows @p tooltip over the item just drawn, the row's preview.
void previewTooltip(const char* tooltip) {
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
}

} // namespace

bool curveWidget(Math::Curve& curve, float lo, float hi, const char* tooltip) {
    ImGui::BeginGroup();
    // Scaled to the curve's own peak, not the key limit, so a low curve under a high limit
    // still shows its shape.
    std::array<float, PREVIEW_SAMPLES> values{};
    float top = lo;
    for (int i = 0; i < PREVIEW_SAMPLES; ++i) {
        values[i] = curve.at(static_cast<float>(i) / static_cast<float>(PREVIEW_SAMPLES - 1));
        top       = std::max(top, values[i]);
    }
    top = top > lo ? lo + (top - lo) * PLOT_HEADROOM : lo + 1.0f;
    const ImVec2 plot(-1.0f, EditorStyle::px(40.0f));
    ImGui::PlotLines("##plot", values.data(), PREVIEW_SAMPLES, 0, nullptr, lo, top, plot);
    previewTooltip(tooltip);

    const auto value = [&](Math::CurveKey& key) {
        return ImGui::DragFloat("##value", &key.value, 0.01f, lo, hi, "%.2f", PROP_CLAMP);
    };
    const auto makeKey = [&](float t) { return Math::CurveKey{t, curve.at(t)}; };
    const bool changed = keyRows(curve.keys, value, makeKey);
    ImGui::EndGroup();
    return changed;
}

bool gradientWidget(Math::Gradient& gradient, const char* tooltip) {
    ImGui::BeginGroup();
    // The strip over ImGui's own checker, so its alpha shows.
    const ImVec2 at     = ImGui::GetCursorScreenPos();
    const float  width  = ImGui::GetContentRegionAvail().x;
    const float  height = ImGui::GetFrameHeight();
    ImDrawList*  draw   = ImGui::GetWindowDrawList();
    const ImVec2 end(at.x + width, at.y + height);
    const ImU32 clear = IM_COL32(0, 0, 0, 0);
    ImGui::RenderColorRectWithAlphaCheckerboard(draw, at, end, clear, height * 0.5f, ImVec2(0.0f, 0.0f));
    const auto colorAt = [&](float t) {
        const glm::vec4 c = gradient.at(t);
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a));
    };
    for (int i = 0; i + 1 < PREVIEW_SAMPLES; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(PREVIEW_SAMPLES - 1);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(PREVIEW_SAMPLES - 1);
        const ImU32 c0 = colorAt(t0);
        const ImU32 c1 = colorAt(t1);
        draw->AddRectFilledMultiColor(
            ImVec2(at.x + width * t0, at.y),
            ImVec2(at.x + width * t1, at.y + height),
            c0,
            c1,
            c1,
            c0
        );
    }
    ImGui::Dummy(ImVec2(width, height));
    previewTooltip(tooltip);

    // A swatch the row wide, its picker a click away: four numbers at this width read "0.c".
    const auto color = [&](Math::GradientKey& key) {
        const ImVec4 shown(key.color.r, key.color.g, key.color.b, key.color.a);
        const ImVec2 size(ImGui::CalcItemWidth(), ImGui::GetFrameHeight());
        if (ImGui::ColorButton("##color", shown, ImGuiColorEditFlags_AlphaPreviewHalf, size)) {
            ImGui::OpenPopup("##picker");
        }
        bool changed = false;
        if (ImGui::BeginPopup("##picker")) {
            const ImGuiColorEditFlags flags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar;
            changed = ImGui::ColorPicker4("##pick", &key.color.r, flags);
            ImGui::EndPopup();
        }
        return changed;
    };
    const auto makeKey = [&](float t) { return Math::GradientKey{t, gradient.at(t)}; };
    const bool changed = keyRows(gradient.keys, color, makeKey);
    ImGui::EndGroup();
    return changed;
}

} // namespace Vkm::Engine
