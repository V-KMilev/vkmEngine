#pragma once

#include <cstdint>
#include <type_traits>

#include "ecs/entity.h"

namespace Vkm::Engine {

template<typename Signature>
class EntityMapping;

/**
 * @brief Maps entities to one carrier's names for them, or back.
 *
 * An EntityId means nothing outside the Scene that minted it, so a reference travels as a number
 * only its carrier (a scene file, a prefab, a connection) can read. Non-owning and type-erased: a
 * carrier passes a stack lambda, no allocation or template. Empty in is empty out, unasked.
 *
 * @tparam R What the carrier maps to.
 * @tparam A What it maps from; zero or a null EntityId is "none".
 */
template<typename R, typename A>
class EntityMapping<R(A)> {
    public:
        /**
         * @brief Wrap @p fn, the carrier's mapping.
         *
         * Excludes its own type, or Fn& would out-match the copy constructor for a non-const lvalue.
         *
         * @tparam Fn Callable as R(A). Must outlive the mapping.
         * @param fn The carrier's mapping; never asked for "none".
         */
        template<typename Fn, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Fn>, EntityMapping>>>
        EntityMapping(Fn& fn) noexcept
            : m_carrier(&fn)
            , m_call([](void* c, A from) -> R { return (*static_cast<Fn*>(c))(from); })
        {}
        ~EntityMapping() = default;

        EntityMapping(const EntityMapping& other) = default;
        EntityMapping& operator=(const EntityMapping& other) = default;

        EntityMapping(EntityMapping && other) = default;
        EntityMapping& operator=(EntityMapping && other) = default;

        /**
         * @brief Map one reference.
         *
         * @param from The entity or name; "none" maps to none without asking the carrier.
         * @return The mapped value, or none when the carrier does not hold it.
         */
        R operator()(A from) const {
            return from ? m_call(m_carrier, from) : R{};
        }

    private:
        void* m_carrier = nullptr;
        R (*m_call)(void*, A) = nullptr;
};

/**
 * @brief Turns one carrier's name for an entity into an entity, for a load.
 */
using EntityResolver = EntityMapping<EntityId(uint32_t)>;

/**
 * @brief Turns an entity into one carrier's name for it, for a save.
 */
using EntityNamer = EntityMapping<uint32_t(EntityId)>;

} // namespace Vkm::Engine
