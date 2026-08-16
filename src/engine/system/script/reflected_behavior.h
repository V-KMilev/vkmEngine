#pragma once

#include <memory>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "core/reflect.h"
#include "system/script/behavior.h"
#include "system/script/behavior_field_visitor.h"

namespace Engine {

namespace detail {

/**
 * @brief Dependent-false helper for a static_assert in an unreachable
 *        `if constexpr` branch (an always-false that mentions V).
 */
template<typename>
inline constexpr bool DEPENDENT_FALSE = false;

/**
 * @brief Route one reflected field to a BehaviorFieldVisitor by its type.
 *
 * The single dispatch point behind ReflectedBehavior::visitFields, split out so
 * it can recurse into nested structs. The leaf check precedes the struct check,
 * so an explicit field() overload for a reflected type wins (edit it atomically
 * rather than descending into it).
 */
template<typename V>
void visitField(BehaviorFieldVisitor& visitor, const char* name, V& value) {
    if constexpr (std::is_enum_v<V>) {
        static_assert(Reflect::HAS_ENUM_NAMES<V>,
            "ReflectedBehavior: enum field needs a VKM_ENUM_NAMES registration.");
        using Names = Reflect::EnumNames<V>;
        int index = static_cast<int>(value);
        visitor.enumField(name, index, Names::values, Names::count);
        value = static_cast<V>(index);
    } else if constexpr (VISITOR_SUPPORTS_FIELD<V>) {
        visitor.field(name, value);
    } else if constexpr (Reflect::IS_REFLECTED<V>) {
        if (visitor.beginStruct(name)) {
            Reflect::forEachField(value, [&](std::string_view subName, auto& sub) {
                visitField(visitor, subName.data(), sub);
            });
            visitor.endStruct();
        }
    } else {
        static_assert(DEPENDENT_FALSE<V>,
            "ReflectedBehavior: field type is not a supported leaf, a "
            "VKM_ENUM_NAMES enum, or a VKM_REFLECT-ed struct. Add a field() "
            "overload in behavior_field_visitor.h, register the type, or drop "
            "the field from VKM_REFLECT.");
    }
}

} // namespace detail

/**
 * @brief CRTP base that derives a behavior's boilerplate from its reflected
 *        fields - the UPROPERTY-equivalent reuse.
 *
 * Declare the tunable fields once with VKM_REFLECT_BEGIN(Derived) / VKM_F /
 * VKM_REFLECT_END and `typeName()`, `visitFields()` (editor + serialization),
 * and `clone()` are all generated. Override the lifecycle hooks
 * (onStart/onUpdate/onDestroy) on the subclass as usual.
 *
 * Requirements on Derived:
 *   - `static constexpr const char* TYPE_NAME` (its BehaviorRegistry key), and
 *   - a `Reflect::Traits<Derived>` specialisation (the VKM_REFLECT markup).
 */
template<typename Derived>
class ReflectedBehavior : public Behavior {
    public:
        const char* typeName() const override { return Derived::TYPE_NAME; }

        void visitFields(BehaviorFieldVisitor& visitor) override {
            Reflect::forEachField(static_cast<Derived&>(*this),
                [&](std::string_view name, auto& value) {
                    // Field names come from string literals (VKM_F's #name), so
                    // data() is null-terminated for the const char* signatures.
                    detail::visitField(visitor, name.data(), value);
                });
        }

        std::unique_ptr<Behavior> clone() const override {
            auto copy = std::make_unique<Derived>();
            const Derived& self = static_cast<const Derived&>(*this);
            // Copy only the reflected (authored) fields; the engine context and
            // started flag are rebound on the new instance by BehaviorSystem.
            std::apply([&](auto&&... f) {
                (((*copy).*(f.ptr) = self.*(f.ptr)), ...);
            }, Reflect::Traits<Derived>::fields());
            return copy;
        }
};

} // namespace Engine
