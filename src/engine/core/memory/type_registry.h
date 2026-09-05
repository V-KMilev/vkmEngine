#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "core/memory/types.h"

namespace Vkm::Engine {

/**
 * @brief One slot per type, indexed by typeId<T>() and filled on first use.
 *
 * Three things in the engine are this container: the Scene's component
 * storages, the ResourceManager's asset slots, and the EventBus's per-event
 * buses. All three index a vector of owning pointers by the process-wide type
 * id, and all three need the vector grown before it is indexed - which is a step
 * that costs nothing to write and everything to forget, so it is written here
 * and nowhere else.
 *
 * The id space is shared with every other registry, so a vector here is as long
 * as the highest id *any* of them has handed out. That is a few hundred null
 * pointers at worst, and it is what buys a lookup that is an array index rather
 * than a hash.
 *
 * @tparam Base What every slot holds - a polymorphic base, or a plain struct
 *              the owner keeps one of per type.
 */
template <typename Base>
class TypeRegistry {
    public:
        TypeRegistry()  = default;
        ~TypeRegistry() = default;

        TypeRegistry(const TypeRegistry& other) = delete;
        TypeRegistry& operator=(const TypeRegistry& other) = delete;

        // Exchanged through swap(), never moved: the three owners hold one by
        // value and are themselves non-movable, so a move here would only be a
        // way to leave one of them empty.
        TypeRegistry(TypeRegistry && other) = delete;
        TypeRegistry& operator=(TypeRegistry && other) = delete;

    public:
        /**
         * @brief The slot for T, created by @p make the first time it is asked for.
         *
         * @tparam T    The type the slot belongs to.
         * @tparam Make Callable returning a std::unique_ptr<Base>.
         * @param make  Builds the slot; called at most once per type.
         * @return The slot, which outlives every call until clear() or swap().
         */
        template <typename T, typename Make>
        Base& ensure(Make&& make) {
            std::unique_ptr<Base>& held = slot<T>();
            if (!held) held = make();
            return *held;
        }

        /**
         * @brief The slot for T without creating one.
         *
         * @tparam T The type the slot belongs to.
         * @return The slot, or null when nothing has asked for T's yet.
         */
        template <typename T>
        Base* find() {
            const TypeId id = typeId<T>();
            return id < m_slots.size() ? m_slots[id].get() : nullptr;
        }

        /// @copydoc find()
        template <typename T>
        const Base* find() const {
            const TypeId id = typeId<T>();
            return id < m_slots.size() ? m_slots[id].get() : nullptr;
        }

        /**
         * @brief The owning pointer for T's slot, grown into existence but left empty.
         *
         * What a caller that wants to move a slot rather than read one needs -
         * the ResourceManager exchanges whole asset slots between two managers
         * on a scene load.
         *
         * @tparam T The type the slot belongs to.
         * @return The slot's owning pointer; null unless something filled it.
         */
        template <typename T>
        std::unique_ptr<Base>& slot() {
            const TypeId id = typeId<T>();
            if (id >= m_slots.size()) m_slots.resize(id + 1);
            return m_slots[id];
        }

        /// Drop every slot. Types are re-registered lazily on the next ask.
        void clear() { m_slots.clear(); }

        /**
         * @brief Exchange every slot with another registry.
         *
         * @param other The registry to trade with.
         */
        void swap(TypeRegistry& other) noexcept { m_slots.swap(other.m_slots); }

        /**
         * @brief How many types actually have a slot.
         *
         * Not the vector's length: that is the highest id any registry has seen,
         * most of which are null here.
         *
         * @return The number of filled slots.
         */
        std::size_t count() const {
            std::size_t n = 0;
            for (const auto& held : m_slots) {
                if (held) ++n;
            }
            return n;
        }

        /**
         * @brief Invoke fn(Base&) for every filled slot, in type-id order.
         *
         * @param fn Callable taking Base&.
         */
        template <typename Fn>
        void forEach(Fn&& fn) {
            for (auto& held : m_slots) {
                if (held) fn(*held);
            }
        }

        /// @copydoc forEach()
        template <typename Fn>
        void forEach(Fn&& fn) const {
            for (const auto& held : m_slots) {
                if (held) fn(*held);
            }
        }

    private:
        std::vector<std::unique_ptr<Base>> m_slots;
};

} // namespace Vkm::Engine
