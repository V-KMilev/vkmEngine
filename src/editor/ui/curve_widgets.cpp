#include "ui/curve_widgets.h"

#include <algorithm>
#include <array>

#include <imgui.h>
#include <imgui_internal.h>

#include "ui/editor_icons.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

namespace {

/// Samples a curve's plot and a gradient's strip take across the span.
constexpr int PREVIEW_SAMPLES = 48;

/// How far a curve's plot reaches past its peak, so the peak does not ride its frame.
constexpr float PLOT_HEADROOM = 1.15f;

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
 * @param keys The keys there are.
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
 * @brief Draw the controls each key of @p keys takes, a row a key, with a remove button, and an
 *        add button under them.
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
    // Only after a time moved, and not mid-drag: rows go by index, so a key reordered under the
    // mouse would hand the drag on. Letting go of a key's time is no edit itself, so its release
    // frame sorts too.
    if (timed && !dragging) sortByTime(keys);
    return changed || timed;
}

/// Shows @p tooltip over the item just drawn, the row's preview.
void previewTooltip(const char* tooltip) {
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
}

} // namespace

bool propCurve(const char* label, Math::Curve& curve, float lo, float hi, const char* tooltip) {
    // The tooltip on the plot alone: over the whole row it would cover each key's own.
    return propRow(label, nullptr, [&] {
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
    });
}

bool propGradient(const char* label, Math::Gradient& gradient, const char* tooltip) {
    // The tooltip on the strip alone, as propCurve's on its plot.
    return propRow(label, nullptr, [&] {
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
    });
}

} // namespace Vkm::Engine
