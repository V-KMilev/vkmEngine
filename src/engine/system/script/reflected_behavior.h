#pragma once

#include <memory>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "core/reflect.h"
#include "resource/asset_ref.h"
#include "resource/asset_type.h"
#include "system/script/behavior.h"
#include "system/script/behavior_field_visitor.h"

namespace Vkm::Engine {

namespace detail {

/**
 * @brief Route one reflected field to a BehaviorFieldVisitor by its type.
 *
 * Leaf before struct, so a field() overload for a reflected type wins (edited
 * atomically); AssetRef sits between, a string leaf that also carries its asset kind.
 *
 * @tparam V Field type; one no branch accepts is a compile error.
 * @param visitor Visitor the field is handed to.
 * @param name Field name, null-terminated.
 * @param value The field itself, read and written in place.
 */
template<typename V>
void visitField(BehaviorFieldVisitor& visitor, const char* name, V& value) {
    if constexpr (std::is_enum_v<V>) {
        static_assert(
            Reflect::HAS_ENUM_NAMES<V>,
            "ReflectedBehavior: enum field needs a VKM_ENUM_NAMES registration."
        );
        using Names = Reflect::EnumNames<V>;
        int index = static_cast<int>(value);
        visitor.enumField(name, index, Names::values, Names::count);
        value = static_cast<V>(index);
    } else if constexpr (VISITOR_SUPPORTS_FIELD<V>) {
        visitor.field(name, value);
    } else if constexpr (IS_ASSET_REF<V>) {
        static_assert(
            ASSET_TYPE<typename V::asset_t> != AssetType::Count,
            "ReflectedBehavior: AssetRef names an asset kind the library does not "
            "hold. Only kinds with an ASSET_TYPE (resource/asset_type.h) can be "
            "authored, because the assets block has no section for the others."
        );
        visitor.assetField(name, value.name, ASSET_TYPE<typename V::asset_t>);
    } else if constexpr (Reflect::IS_REFLECTED<V>) {
        if (visitor.beginStruct(name)) {
            Reflect::forEachField(value, [&](std::string_view subName, auto& sub) {
                visitField(visitor, subName.data(), sub);
            });
            visitor.endStruct();
        }
    } else {
        static_assert(
            Reflect::DEPENDENT_FALSE<V>,
            "ReflectedBehavior: a reflected field must be float, int, bool, std::string, "
            "glm::vec2, glm::vec3, glm::vec4, glm::quat, an enum registered with "
            "VKM_ENUM_NAMES, an AssetRef<Asset>, or a struct with its own reflect block. "
            "Keep anything else as runtime state: leave it out of VKM_REFLECT."
        );
    }
}

} // namespace detail

/**
 * @brief CRTP base that derives typeName(), visitFields() and clone() from the reflect block.
 *
 * The block (VKM_REFLECT_BEGIN(::Game::Derived) / VKM_F / VKM_REFLECT_END) is required,
 * even if empty. The name is the unqualified class name, which scenes store, so
 * renaming the class renames the behavior.
 */
template<typename Derived>
class ReflectedBehavior : public Behavior {
    public:
        const char* typeName() const override { return reflectedName(); }

        void visitFields(BehaviorFieldVisitor& visitor) override {
            Reflect::forEachField(
                static_cast<Derived&>(*this),
                [&](std::string_view name, auto& value) {
                    // VKM_F's #name is a literal, so data() is null-terminated.
                    detail::visitField(visitor, name.data(), value);
                }
            );
        }

        std::unique_ptr<Behavior> clone() const override {
            auto copy = std::make_unique<Derived>();
            const Derived& self = static_cast<const Derived&>(*this);
            std::apply(
                [&](auto&&... f) {
                    (((*copy).*(f.ptr) = self.*(f.ptr)), ...);
                },
                Reflect::Traits<Derived>::fields()
            );
            return copy;
        }

    private:
        /**
         * @brief Derived's name from its reflect block, or the error that says how to write one.
         *
         * A function, since Derived is incomplete while this base is instantiated.
         *
         * @return Reflect::Traits<Derived>::NAME.
         */
        static constexpr const char* reflectedName() {
            static_assert(
                Reflect::IS_REFLECTED<Derived>,
                "This behavior has no reflect block. Below the class, at global scope, write "
                "VKM_REFLECT_BEGIN(::YourNamespace::YourBehavior), one VKM_F(field) per authored "
                "field, then VKM_REFLECT_END() - with no fields between them if it has none."
            );
            if constexpr (Reflect::IS_REFLECTED<Derived>) return Reflect::Traits<Derived>::NAME;
            else return "";
        }
};

} // namespace Vkm::Engine
