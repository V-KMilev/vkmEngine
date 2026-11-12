#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include <imgui.h>
#include <glm/glm.hpp>

#include "base/math/curve.h"
#include "base/math/float_range.h"
#include "base/reflect/reflect.h"
#include "base/reflect/enum_names.h"
#include "io/json/reflect_json.h"
#include "resource/handle.h"
#include "base/reflect/field_info.h"

#include "ui/asset/asset_combo.h"
#include "ui/editor_widgets.h"
#include "ui/field_label.h"

#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief A curve's plot and its keys under it, filling the row's control column.
 *
 * @param curve Curve whose keys it edits; left in time order.
 * @param lo    Lowest a key's value may be.
 * @param hi    Highest a key's value may be. The plot spans lo to just past the curve's peak.
 * @param tooltip Shown over the plot, or null.
 * @return Whether it was edited this frame.
 */
bool curveWidget(Math::Curve& curve, float lo, float hi, const char* tooltip);

/**
 * @brief A gradient's strip and its keys under it, filling the row's control column.
 *
 * @param gradient Gradient whose keys it edits; left in time order.
 * @param tooltip  Shown over the strip, or null.
 * @return Whether it was edited this frame.
 */
bool gradientWidget(Math::Gradient& gradient, const char* tooltip);

class Scene;

class ResourceManager;
class CommandHost;

/**
 * @brief What a row's member type does not decide.
 */
enum class Shown : uint8_t { Plain, Slider, Angle, Color };

/**
 * @brief Where a row naming an asset lists and loads them.
 */
struct AssetSlots {
    ResourceManager& resources;
    CommandHost&     host;
};

bool editFloat(const FieldInfo& info, float& value, const char* format, Shown shown);
bool editInt(const FieldInfo& info, int& value, Shown shown);
bool editU32(const FieldInfo& info, uint32_t& value);
bool editVec2(const FieldInfo& info, glm::vec2& value, const char* format);
bool editVec3(const FieldInfo& info, glm::vec3& value, const char* format, Shown shown);
bool editVec4(const FieldInfo& info, glm::vec4& value, const char* format, Shown shown);
bool editRange(const FieldInfo& info, Math::FloatRange& value, const char* format, Shown shown);
bool editCurve(const FieldInfo& info, Math::Curve& value);
bool editGradient(const FieldInfo& info, Math::Gradient& value);

/**
 * @brief The widget a value's type edits with, filling the row's control column.
 *
 * The field's VKM_F range bounds it, typed input included; a name ending in "color", or Shown::Color,
 * makes a vector a color. An angle is held in radians and shown in degrees.
 *
 * @param info   The field: its name, range and tip.
 * @param value  Edited in place.
 * @param format printf format for the shown number, null for the type's own.
 * @param shown  What the type does not decide: a slider, an angle, a color.
 * @param assets Where a handle's assets are listed; null where no row names one.
 * @return Whether it was edited this frame.
 */
template <typename M>
bool editWidget(const FieldInfo& info, M& value, const char* format, Shown shown, const AssetSlots* assets) {
    if constexpr (std::is_same_v<M, bool>) {
        return ImGui::Checkbox("##v", &value);
    } else if constexpr (std::is_same_v<M, float>) {
        return editFloat(info, value, format, shown);
    } else if constexpr (std::is_same_v<M, int>) {
        return editInt(info, value, shown);
    } else if constexpr (std::is_same_v<M, uint32_t>) {
        return editU32(info, value);
    } else if constexpr (std::is_same_v<M, uint16_t>) {
        uint32_t staged = value;
        if (!editU32(info, staged)) return false;
        value = static_cast<uint16_t>(staged);
        return true;
    } else if constexpr (std::is_same_v<M, glm::vec2>) {
        return editVec2(info, value, format);
    } else if constexpr (std::is_same_v<M, glm::vec3>) {
        return editVec3(info, value, format, shown);
    } else if constexpr (std::is_same_v<M, glm::vec4>) {
        return editVec4(info, value, format, shown);
    } else if constexpr (std::is_same_v<M, std::string>) {
        ImGui::SetNextItemWidth(-1.0f);
        return ImGui::InputText("##v", &value);
    } else if constexpr (std::is_same_v<M, Math::FloatRange>) {
        return editRange(info, value, format, shown);
    } else if constexpr (std::is_same_v<M, Math::Curve>) {
        return editCurve(info, value);
    } else if constexpr (std::is_same_v<M, Math::Gradient>) {
        return editGradient(info, value);
    } else if constexpr (std::is_enum_v<M> && Reflect::HAS_ENUM_NAMES<M>) {
        return drawEnumCombo("##v", value);
    } else if constexpr (ReflectJson::IsHandle<M>::value) {
        using Asset = typename M::resource_t;
        return pickAsset<Asset>("##v", nullptr, assets->resources, assets->host, value);
    } else {
        static_assert(Reflect::DEPENDENT_FALSE<M>, "no widget edits this type: the card's own does");
        return false;
    }
}

/**
 * @brief A row of its own: the field's label, the widget editWidget picks, its tip over both.
 *
 * @param info   The field.
 * @param value  Edited in place.
 * @param format As editWidget's.
 * @param shown  As editWidget's.
 * @param assets As editWidget's.
 * @return Whether it was edited this frame.
 */
template <typename M>
bool editValue(
    const FieldInfo& info,
    M& value,
    const char* format = nullptr,
    Shown shown = Shown::Plain,
    const AssetSlots* assets = nullptr
) {
    const std::string label = fieldLabel(info.name);
    drawPropertyLabel(label.c_str());
    if (info.tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", info.tip);
    ImGui::PushID(info.name);
    const bool changed = editWidget(info, value, format, shown, assets);
    if (info.tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", info.tip);
    ImGui::PopID();
    return changed;
}

/**
 * @brief What a right click on a card's row asked of it.
 */
enum class RowAsk { None, Reset, Revert };

/**
 * @brief A card row's label: in the accent while the prefab instance overrides it, its tip on
 *        hover, and a right click offering Reset to Default and, overridden, Revert to Prefab.
 *
 * @param label      The row's words.
 * @param tip        Shown over it, or null.
 * @param overridden Whether the instance overrides the field.
 * @return What the menu asked this frame.
 */
RowAsk rowLabel(const char* label, const char* tip, bool overridden);

/**
 * @brief XYZ vector row a card draws by hand, such as a rotation shown as Euler angles: three drags,
 *        each marked by its axis's color, under a card row's label (rowLabel), with no override mark.
 *
 * Bounds hold on typed input too (PROP_CLAMP); lo == hi == 0 leaves an axis unbounded.
 *
 * @param label      Row label.
 * @param tip        Shown over the label, or null.
 * @param values     The three floats edited.
 * @param resetValue What Reset to Default puts in each.
 * @param speed      Drag speed per axis.
 * @param lo         Lower bound.
 * @param hi         Upper bound.
 * @return True the frame any axis is edited.
 */
bool drawVec3Control(
    const char* label,
    const char* tip,
    float* values,
    float resetValue = 0.0f,
    float speed = 0.1f,
    float lo = 0.0f,
    float hi = 0.0f
);

/**
 * @brief A row naming another entity of the scene, picked from all but @p self.
 *
 * Read-only in a prefab instance: an override cannot name an entity, so a pick would not load.
 *
 * @param scene  Scene holding both.
 * @param self   Entity whose card is being drawn.
 * @param label  The row's name.
 * @param target The reference edited.
 * @param tip    What the reference is for, or null.
 * @return True when the pick changed @p target.
 */
bool propEntity(Scene& scene, EntityId self, const char* label, EntityId& target, const char* tip);

} // namespace Vkm::Engine
