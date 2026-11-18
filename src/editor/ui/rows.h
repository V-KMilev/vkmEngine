#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <imgui.h>

#include "base/reflect/reflect.h"
#include "ecs/entity.h"
#include "ui/field_label.h"

#include "ui/value_widgets.h"

namespace Vkm::Engine {

class Scene;

namespace RowsDetail {

/// The index of @p Member among T's reflected fields, or -1 when T does not reflect it.
template <typename T, auto Member, std::size_t I = 0>
constexpr int fieldIndex() {
    constexpr auto fields = Reflect::Traits<T>::fields();
    if constexpr (I == std::tuple_size_v<decltype(fields)>) {
        return -1;
    } else {
        constexpr auto field = std::get<I>(fields);
        if constexpr (std::is_same_v<decltype(field.ptr), decltype(Member)>) {
            if constexpr (field.ptr == Member) return static_cast<int>(I);
            else return fieldIndex<T, Member, I + 1>();
        } else {
            return fieldIndex<T, Member, I + 1>();
        }
    }
}

/// The bit an edit of @p Member sets in a card's shared mask: none for a list, the active entity's alone.
template <typename T, auto Member>
constexpr uint64_t shareBit() {
    constexpr int INDEX = fieldIndex<T, Member>();
    static_assert(INDEX >= 0, "a member of its struct's reflect block");
    static_assert(INDEX < 64, "a card's shared mask holds a bit for each of its struct's first 64 fields");
    using Value = std::decay_t<decltype(std::declval<T&>().*Member)>;
    return ReflectJson::IsVector<Value>::value ? 0 : uint64_t{1} << INDEX;
}

} // namespace RowsDetail

/**
 * @brief The rows of a reflected T, each naming its member at compile time and labeled by its VKM_F.
 *
 * The mask of fields edited, by reflected index, is what an edit carries to the rest of the selection;
 * an edit the card's own code makes (edited) is the active entity's alone.
 *
 * @tparam T The component; only a reflected one has rows, and any has value() and edited().
 */
template <typename T>
class Rows {
    public:
        /**
         * @param value      What the rows edit, in place.
         * @param overridden The field keys a prefab instance overrides, marked on their rows.
         * @param assets     Where a handle's row lists its assets; null when T names none.
         */
        Rows(T& value, std::vector<std::string> overridden, const AssetSlots* assets)
            : m_value(value)
            , m_overridden(std::move(overridden))
            , m_assets(assets) {}
        ~Rows() = default;

        Rows(const Rows& other) = delete;
        Rows& operator=(const Rows& other) = delete;

        Rows(Rows && other) = delete;
        Rows& operator=(Rows && other) = delete;

    public:
        /**
         * @brief A row for @p Member: the widget its type picks, its VKM_F range and tip.
         *
         * @param format printf format for its number, null for the type's own.
         * @param shown  What the type does not decide.
         * @return Whether it was edited this frame.
         */
        template <auto Member>
        bool row(const char* format = nullptr, Shown shown = Shown::Plain) {
            return rowWith<Member>([&](auto& value, const FieldInfo& info) {
                return editWidget(info, value, format, shown, m_assets);
            });
        }

        /**
         * @brief A row for @p Member drawn by the card's @p widget, labeled, marked and shared as row()'s is.
         *
         * @param widget Called with the member and its FieldInfo; draws the control and returns
         *               whether it edited the member.
         * @return Whether it was edited this frame.
         */
        template <auto Member, typename Widget>
        bool rowWith(Widget&& widget) {
            constexpr int INDEX = RowsDetail::fieldIndex<T, Member>();
            static_assert(INDEX >= 0, "a row names a member of its struct's reflect block");
            constexpr auto field = std::get<INDEX>(Reflect::Traits<T>::fields());
            const FieldInfo info{field.name.data(), field.tip, field.min, field.max};

            const std::string label = fieldLabel(field.name);
            const RowAsk asked = rowLabel(label.c_str(), info.tip, isOverridden(info.name));
            ImGui::PushID(info.name);
            bool changed = widget(m_value.*Member, info);
            if (info.tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", info.tip);
            ImGui::PopID();
            if (asked == RowAsk::Reset) {
                m_value.*Member = T{}.*Member;
                changed = true;
            }
            if (asked == RowAsk::Revert) m_revert = info.name;
            if (changed) m_shared |= RowsDetail::shareBit<T, Member>();
            // A list is the active entity's alone: one item edited must not replace another's list.
            if (changed && RowsDetail::shareBit<T, Member>() == 0) m_edited = true;
            return changed;
        }

        /// row() as a slider across the field's range.
        template <auto Member>
        bool slider(const char* format = nullptr) { return row<Member>(format, Shown::Slider); }

        /// row() over an angle held in radians, shown in degrees.
        template <auto Member>
        bool angle(const char* format = nullptr) { return row<Member>(format, Shown::Angle); }

        /**
         * @brief The rows of the reflected struct @p Member holds, drawn by @p rowsOfMember.
         *
         * Marked, reverted and shared as one field: the member.
         *
         * @param rowsOfMember Called with a Rows over the member.
         * @return Whether any of its rows was edited.
         */
        template <auto Member, typename Fn>
        bool nested(Fn&& rowsOfMember) {
            constexpr int INDEX = RowsDetail::fieldIndex<T, Member>();
            static_assert(INDEX >= 0, "a nested struct is a member of its struct's reflect block");
            const bool changed = rowsOf<INDEX>(m_value.*Member, rowsOfMember);
            if (changed) m_shared |= RowsDetail::shareBit<T, Member>();
            return changed;
        }

        /**
         * @brief Rows over each element of the list @p Member holds, drawn by @p rowsOfItem.
         *
         * Marked and reverted as one field, the list. A list is the active entity's alone: one item
         * edited must not replace another entity's list.
         *
         * @param rowsOfItem Called with a Rows over each element and its index, inside an id scope.
         * @return Whether any of them was edited.
         */
        template <auto Member, typename Fn>
        bool each(Fn&& rowsOfItem) {
            constexpr int INDEX = RowsDetail::fieldIndex<T, Member>();
            static_assert(INDEX >= 0, "a list is a member of its struct's reflect block");
            auto& list    = m_value.*Member;
            bool  changed = false;
            for (std::size_t i = 0; i < list.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                changed |= rowsOf<INDEX>(list[i], [&](auto& item) { rowsOfItem(item, i); });
                ImGui::PopID();
            }
            if (changed) m_edited = true;
            return changed;
        }

        /// @p Member for the card's own widgets; their edit is this entity's alone, reported by edited().
        template <auto Member>
        auto& custom() { return m_value.*Member; }

        /// @p Member's VKM_F description, the tip a card's own widgets for it show.
        template <auto Member>
        static const char* tipOf() {
            constexpr int INDEX = RowsDetail::fieldIndex<T, Member>();
            static_assert(INDEX >= 0, "a member of its struct's reflect block");
            return std::get<INDEX>(Reflect::Traits<T>::fields()).tip;
        }

        /// The card's own code changed the value: this entity alone takes it.
        void edited() { m_edited = true; }

        /// The card's own widget changed @p Member: shared as a row's edit is.
        template <auto Member>
        void edited() {
            m_shared |= RowsDetail::shareBit<T, Member>();
        }

        T& value() { return m_value; }

        /// The fields the rows edited, bit i for reflected field i.
        uint64_t shared() const { return m_shared; }

        /// Whether anything edited the value this frame.
        bool changed() const { return m_edited || m_shared != 0; }

        /// The field key a row's menu asked to revert to the prefab's value, if one did.
        const std::optional<std::string>& revertAsked() const { return m_revert; }

    private:
        template <typename>
        friend class Rows;

    private:
        bool isOverridden(const char* key) const {
            if (m_whole) return true;
            for (const std::string& field : m_overridden) {
                if (field == key) return true;
            }
            return false;
        }

        /**
         * @brief Rows over @p value, a part of reflected field @p INDEX, drawn by @p draw.
         *
         * An instance overrides a field whole, so each row of the part is marked when the field is, and
         * a Revert asked on one reverts the field.
         *
         * @return Whether any of its rows was edited.
         */
        template <int INDEX, typename Inner, typename Fn>
        bool rowsOf(Inner& value, Fn&& draw) {
            constexpr auto field = std::get<INDEX>(Reflect::Traits<T>::fields());
            Rows<Inner> inner(value, {}, m_assets);
            inner.m_whole = isOverridden(field.name.data());
            draw(inner);
            if (inner.revertAsked()) m_revert = std::string(field.name);
            return inner.changed();
        }

    private:
        T&                         m_value;
        std::vector<std::string>   m_overridden;
        const AssetSlots*          m_assets = nullptr;
        uint64_t                   m_shared = 0;
        bool                       m_edited = false;
        bool                       m_whole  = false;  ///< Every row is overridden: the value is a part of
                                                      ///< a field its instance overrides.
        std::optional<std::string> m_revert;
};

} // namespace Vkm::Engine
