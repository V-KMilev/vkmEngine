#pragma once

#include <cstdint>
#include <type_traits>

#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief Turns one carrier's name for an entity into an entity.
 *
 * A cross-entity reference stops being a handle the moment it leaves the Scene
 * that minted it: the generation belonged to that session and the slot to that
 * session's allocator. What travels instead is a number meaningful only to
 * whatever carries it - a slot in a scene file, a uid in a prefab, a server's
 * name on a connection - so only the carrier can read it back.
 *
 * A component that references entities therefore takes one of these on load,
 * exactly as a component that references assets takes the ResourceManager: a
 * reference cannot be read back without naming the carrier it came from.
 *
 * Non-owning and type-erased, so a carrier hands in a stack lambda without an
 * allocation and without every loader becoming a template. The constructor
 * excludes its own type: an unconstrained template taking Fn& is a better match
 * for a non-const lvalue of this class than the copy constructor is, so copying
 * one would have wrapped it in itself.
 */
class EntityResolver {
    public:
        /**
         * @brief Wrap @p fn, which maps this carrier's number to an entity.
         *
         * @tparam Fn Callable as EntityId(uint32_t). Must outlive the resolver.
         * @param fn The carrier's mapping. Zero is always "no entity".
         */
        template<typename Fn,
                 typename = std::enable_if_t<
                     !std::is_same_v<std::decay_t<Fn>, EntityResolver>>>
        EntityResolver(Fn& fn) noexcept
            : m_carrier(&fn),
              m_call([](void* c, uint32_t name) {
                  return (*static_cast<Fn*>(c))(name);
              }) {}

        /**
         * @brief Resolve one reference.
         *
         * @param name The carrier's number for an entity; zero is none.
         * @return The entity, or a null id when the carrier does not hold one.
         */
        EntityId operator()(uint32_t name) const {
            return name == 0 ? EntityId{} : m_call(m_carrier, name);
        }

    private:
        void*    m_carrier = nullptr;
        EntityId (*m_call)(void*, uint32_t) = nullptr;
};

/**
 * @brief Turns an entity into one carrier's name for it.
 *
 * The save-side half of EntityResolver. A scene names an entity by its slot
 * and a prefab by its place in the file, so the same component writes
 * different numbers into different carriers without knowing which it is in.
 */
class EntityNamer {
    public:
        /**
         * @brief Wrap @p fn, which maps an entity to this carrier's number.
         *
         * @tparam Fn Callable as uint32_t(EntityId). Must outlive the namer.
         * @param fn The carrier's mapping. An entity it does not carry is zero.
         */
        template<typename Fn,
                 typename = std::enable_if_t<
                     !std::is_same_v<std::decay_t<Fn>, EntityNamer>>>
        EntityNamer(Fn& fn) noexcept
            : m_carrier(&fn),
              m_call([](void* c, EntityId id) {
                  return (*static_cast<Fn*>(c))(id);
              }) {}

        /**
         * @brief Name one entity.
         *
         * @param id Entity being written.
         * @return The carrier's number for it, or zero for none.
         */
        uint32_t operator()(EntityId id) const {
            return id ? m_call(m_carrier, id) : 0u;
        }

    private:
        void*    m_carrier = nullptr;
        uint32_t (*m_call)(void*, EntityId) = nullptr;
};

} // namespace Vkm::Engine
