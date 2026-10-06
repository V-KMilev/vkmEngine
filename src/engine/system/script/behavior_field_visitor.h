#pragma once

#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "resource/asset_type.h"

namespace Vkm::Engine {

/**
 * @brief Type-erased visitor over a behavior's reflected authoring fields.
 *
 * `ReflectedBehavior::visitFields` dispatches each field: leaves to a `field()`
 * overload (the one closed set; add an overload for a new type), VKM_ENUM_NAMES enums
 * to `enumField()`, AssetRef<Asset> to `assetField()`, and VKM_REFLECT-ed structs
 * through beginStruct()/endStruct().
 */
class BehaviorFieldVisitor {
    public:
        BehaviorFieldVisitor() = default;
        virtual ~BehaviorFieldVisitor() = default;

        BehaviorFieldVisitor(const BehaviorFieldVisitor& other) = delete;
        BehaviorFieldVisitor& operator=(const BehaviorFieldVisitor& other) = delete;

        BehaviorFieldVisitor(BehaviorFieldVisitor && other) = delete;
        BehaviorFieldVisitor& operator=(BehaviorFieldVisitor && other) = delete;

    public:
        virtual void field(const char* name, float& value)       = 0;
        virtual void field(const char* name, int& value)         = 0;
        virtual void field(const char* name, bool& value)        = 0;
        virtual void field(const char* name, glm::vec2& value)   = 0;
        virtual void field(const char* name, glm::vec3& value)   = 0;
        virtual void field(const char* name, glm::vec4& value)   = 0;
        virtual void field(const char* name, glm::quat& value)   = 0;
        virtual void field(const char* name, std::string& value) = 0;

        /**
         * @brief Visit an enum field, type-erased to (name index, name table).
         *
         * Serialize by name (names[index]), never the integer, so reordering values
         * keeps scenes valid.
         *
         * @param name  Field name.
         * @param index In: current value. Out: the visitor's chosen value.
         * @param names Value-ordered names[count] from the enum's EnumNames.
         * @param count Number of names.
         */
        virtual void enumField(const char* name, int& index, const char* const* names, std::size_t count) = 0;

        /**
         * @brief Visit a field that names an asset, type-erased to (name, kind).
         *
         * The name, not a handle, is the serializable identity; a serializer needs @p type
         * to save the asset too, or the reference resolves to nothing on load.
         *
         * @param name      Field name.
         * @param assetName In: the current reference, empty for none. Out: the
         *                  visitor's chosen one.
         * @param type      Asset kind the field may reference.
         */
        virtual void assetField(const char* name, std::string& assetName, AssetType type) = 0;

        /**
         * @brief Enter a nested reflected-struct field; return true to descend.
         *
         * False skips the children and no endStruct() follows, so open per-struct scope
         * (a JSON sub-object, a tree node) only on the true path.
         *
         * @param name Field name of the nested struct.
         * @return True to visit its sub-fields and then endStruct().
         */
        virtual bool beginStruct(const char* name) = 0;

        /**
         * @brief Leave the struct opened by a beginStruct() that returned true.
         */
        virtual void endStruct() = 0;
};

/**
 * @brief True iff BehaviorFieldVisitor has a field() overload accepting V&.
 *
 * Derived from the overload set. The diagnostic is detail::visitField's static_assert.
 *
 * @tparam V Field type asked about.
 */
template<typename V, typename = void>
inline constexpr bool VISITOR_SUPPORTS_FIELD = false;

template<typename V>
inline constexpr bool VISITOR_SUPPORTS_FIELD<
    V,
    std::void_t<decltype(
        std::declval<BehaviorFieldVisitor&>().field(std::declval<const char*>(), std::declval<V&>())
    )>
> = true;

} // namespace Vkm::Engine
