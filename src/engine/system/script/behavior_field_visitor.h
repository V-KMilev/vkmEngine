#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include <glm/glm.hpp>

namespace Vkm::Engine {

/**
 * @brief Type-erased visitor over a behavior's reflected authoring fields.
 *
 * The bridge that lets code holding only a `Behavior*` (the editor inspector,
 * the serializer) read or write a concrete behavior's fields without knowing
 * its type: `ReflectedBehavior::visitFields` walks the reflected fields and
 * dispatches each to the method below that fits it. Implement one of these to
 * serialize, edit, or otherwise process every field uniformly.
 *
 * Three shapes of field reach a visitor, and together they keep the authorable
 * type space effectively open without growing this interface per game type:
 *  - A leaf value goes to the matching `field()` overload. This is the only
 *    closed set; add an overload to support a new leaf type (a behavior that
 *    reflects an unsupported leaf then fails to compile, which is the right
 *    nudge). Its widget/serialization is inherently engine/editor-owned.
 *  - Any VKM_ENUM_NAMES enum goes to `enumField()` - one method covers every
 *    enum, so game enums need no change here.
 *  - Any VKM_REFLECT-ed struct is descended into via beginStruct()/endStruct(),
 *    so composites built from the above need no change here either.
 */
class BehaviorFieldVisitor {
    public:
        virtual ~BehaviorFieldVisitor() = default;

        virtual void field(const char* name, float& value)     = 0;
        virtual void field(const char* name, int& value)       = 0;
        virtual void field(const char* name, bool& value)      = 0;
        virtual void field(const char* name, glm::vec3& value) = 0;

        /**
         * @brief Visit an enum field, type-erased to (name index, name table).
         *
         * ReflectedBehavior routes every VKM_ENUM_NAMES enum here instead of
         * adding a `field()` overload per enum: it passes the current value as
         * an index into @p names and reads @p index back, so a mutating visitor
         * (loader, inspector) can change the selection. Serialize by name
         * (names[index]), never the raw integer, so reordering enum values
         * without renaming keeps existing scenes valid.
         *
         * @param name  Field name.
         * @param index In: current value. Out: the visitor's chosen value.
         * @param names Value-ordered names[count] from the enum's EnumNames.
         * @param count Number of names.
         */
        virtual void enumField(const char* name, int& index,
                               const char* const* names, std::size_t count) = 0;

        /**
         * @brief Enter a nested reflected-struct field; return true to descend.
         *
         * ReflectedBehavior calls this for a field whose type is itself
         * VKM_REFLECT-ed, then visits its sub-fields, then endStruct(). A visitor
         * that opens per-struct scope here (a JSON sub-object, an inspector tree
         * node) must return true; returning false skips the children and no
         * matching endStruct() follows - so a visitor only opens scope on the
         * true path (the inspector uses false for a collapsed header, the loader
         * for a missing sub-object).
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
 * Derived from the overload set itself, not a hand-maintained list, so it stays
 * in sync automatically: add an overload above and V becomes supported here with
 * no other change. ReflectedBehavior::visitFields static_asserts on this to turn
 * an unsupported reflected field type into one readable error instead of
 * overload-resolution soup.
 */
template<typename V, typename = void>
inline constexpr bool VISITOR_SUPPORTS_FIELD = false;

template<typename V>
inline constexpr bool VISITOR_SUPPORTS_FIELD<V, std::void_t<
    decltype(std::declval<BehaviorFieldVisitor&>().field(
        std::declval<const char*>(), std::declval<V&>()))>> = true;

} // namespace Vkm::Engine
