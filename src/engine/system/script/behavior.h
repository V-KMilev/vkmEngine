#pragma once

#include <memory>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

#include "base/reflect/reflect.h"
#include "base/reflect/enum_names.h"
#include "io/scene/prefab_ref.h"
#include "resource/asset_ref.h"
#include "resource/asset_type.h"
#include "system/script/behavior_base.h"
#include "system/script/behavior_field_visitor.h"
#include "base/reflect/field_info.h"
#include "system/script/behavior_registry.h"
#include "system/script/module/module_entry.h"

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
 * @param info The field's name and what its VKM_F row says.
 * @param value The field itself, read and written in place.
 */
template<typename V>
void visitField(BehaviorFieldVisitor& visitor, const FieldInfo& info, V& value);

/// Hand @p object's @p field to @p visitor, with what its VKM_F row says.
template<typename T, typename F>
void visitOneField(BehaviorFieldVisitor& visitor, const F& field, T& object) {
    // VKM_F's #name is a literal, so data() is null-terminated.
    visitField(visitor, FieldInfo{field.name.data(), field.tip, field.min, field.max}, object.*(field.ptr));
}

/// Hand each of @p object's reflected fields to @p visitor, in block order.
template<typename T>
void visitFieldsOf(BehaviorFieldVisitor& visitor, T& object) {
    std::apply(
        [&](const auto&... field) { (visitOneField(visitor, field, object), ...); },
        Reflect::Traits<T>::fields()
    );
}

template<typename V>
void visitField(BehaviorFieldVisitor& visitor, const FieldInfo& info, V& value) {
    if constexpr (std::is_enum_v<V>) {
        static_assert(
            Reflect::HAS_ENUM_NAMES<V>,
            "Behavior: enum field needs a VKM_ENUM_NAMES registration."
        );
        using Names = Reflect::EnumNames<V>;
        int index = static_cast<int>(value);
        visitor.enumField(info, index, Names::values, Names::count);
        value = static_cast<V>(index);
    } else if constexpr (VISITOR_SUPPORTS_FIELD<V>) {
        visitor.field(info, value);
    } else if constexpr (IS_ASSET_REF<V>) {
        static_assert(
            ASSET_TYPE<typename V::asset_t> != AssetType::Count,
            "Behavior: AssetRef names an asset kind the library does not "
            "hold. Only kinds with an ASSET_TYPE (resource/asset_type.h) can be "
            "authored, because the assets block has no section for the others."
        );
        visitor.assetField(info, value.name, ASSET_TYPE<typename V::asset_t>);
    } else if constexpr (std::is_same_v<V, PrefabRef>) {
        visitor.prefabField(info, value.path);
    } else if constexpr (Reflect::IS_REFLECTED<V>) {
        if (visitor.beginStruct(info)) {
            visitFieldsOf(visitor, value);
            visitor.endStruct();
        }
    } else {
        static_assert(
            Reflect::DEPENDENT_FALSE<V>,
            "Behavior: a reflected field must be float, int, bool, std::string, "
            "glm::vec2, glm::vec3, glm::vec4, glm::quat, an enum registered with "
            "VKM_ENUM_NAMES, an AssetRef<Asset>, a PrefabRef, or a struct with its own reflect block. "
            "Keep anything else as runtime state: leave it out of the VKM_BEHAVIOR block."
        );
    }
}

/// Whether T's reflect block is a VKM_BEHAVIOR block, which lists it in its module.
template<typename T, typename = void>
inline constexpr bool IS_LISTED = false;

template<typename T>
inline constexpr bool IS_LISTED<T, std::void_t<decltype(Reflect::Traits<T>::vkm_listed)>> = true;

} // namespace detail

/**
 * @brief CRTP base that derives typeName(), visitFields() and clone() from the reflect block.
 *
 * The block (VKM_BEHAVIOR(::Game::Derived) / VKM_F / VKM_REFLECT_END) is required, even if
 * empty, and registers the behavior. The name is the unqualified class name, which scenes
 * store, so renaming the class renames the behavior. A field's name is its key in a file: a
 * missing key keeps the field's value, and one of the wrong type keeps it with a warning. A
 * glm::quat field edits as Euler angles in degrees and saves as the quaternion.
 */
template<typename Derived>
class Behavior : public BehaviorBase {
    public:
        const char* typeName() const override { return reflectedName(); }

        void visitFields(BehaviorFieldVisitor& visitor) override {
            detail::visitFieldsOf(visitor, static_cast<Derived&>(*this));
        }

        std::unique_ptr<BehaviorBase> clone() const override {
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
                detail::IS_LISTED<Derived>,
                "This behavior has no VKM_BEHAVIOR block. Below the class, at global scope, write "
                "VKM_BEHAVIOR(::YourNamespace::YourBehavior), one VKM_F(field) per authored "
                "field, then VKM_REFLECT_END() - with no fields between them if it has none."
            );
            if constexpr (detail::IS_LISTED<Derived>) return Reflect::Traits<Derived>::NAME;
            else return "";
        }
};

/**
 * @brief Put T on this module's list as the module is mapped; VKM_BEHAVIOR's.
 *
 * Only listed: the registry is filled when the host calls vkmRegisterBehaviors, after it has
 * released the copy before, so a reload registers as a first load does.
 *
 * @return True, for the static it initializes.
 */
template<typename T>
bool listModuleBehavior() {
    vkmModuleBehaviors().push_back([] { BehaviorRegistry::get().registerBehaviors<T>(); });
    return true;
}

} // namespace Vkm::Engine

/**
 * @brief A behavior's reflect block, which also registers it in its module.
 *
 * Written as VKM_REFLECT_BEGIN is, below the class at global scope, with the VKM_F rows and
 * VKM_REFLECT_END() after it. Behavior<T> refuses to compile without one. It registers only from
 * a file the module compiles: a behavior whose header no .cpp includes is not registered.
 */
#define VKM_BEHAVIOR(Type) \
    VKM_REFLECT_TRAITS(Type, static inline const bool vkm_listed = ::Vkm::Engine::listModuleBehavior<Type>();)
