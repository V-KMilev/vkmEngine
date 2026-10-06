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
 * Type ids are shared across registries, so slots for other registries' types
 * sit empty.
 *
 * @tparam Base What every slot holds: a polymorphic base or a per-type struct.
 */
template <typename Base>
class TypeRegistry {
    public:
        TypeRegistry()  = default;
        ~TypeRegistry() = default;

        TypeRegistry(const TypeRegistry& other) = delete;
        TypeRegistry& operator=(const TypeRegistry& other) = delete;

        // Exchanged through swap(), never moved.
        TypeRegistry(TypeRegistry && other) = delete;
        TypeRegistry& operator=(TypeRegistry && other) = delete;

    public:
        /**
         * @brief The slot for T, created by @p make the first time it is asked for.
         *
         * @tparam T    Type the slot belongs to.
         * @tparam Make Callable returning a std::unique_ptr<Base>.
         * @param make  Builds the slot; called at most once per type.
         * @return The slot, stable until clear() or swap().
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
         * @tparam T Type the slot belongs to.
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
         * For a caller moving a slot rather than reading it.
         *
         * @tparam T Type the slot belongs to.
         * @return The owning pointer; null unless something filled it.
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
         * @brief Empty every slot @p pred accepts, keeping the rest where they are.
         *
         * Never erased: the index is the type id, so closing a gap would hand
         * later types the wrong slot.
         *
         * @tparam Pred Callable taking const Base& and returning bool.
         * @param pred True for a slot to drop.
         */
        template <typename Pred>
        void removeIf(Pred&& pred) {
            for (auto& held : m_slots) {
                if (held && pred(static_cast<const Base&>(*held))) held.reset();
            }
        }

        /**
         * @brief Exchange every slot with another registry.
         *
         * @param other Registry to trade with.
         */
        void swap(TypeRegistry& other) noexcept { m_slots.swap(other.m_slots); }

        /**
         * @brief How many types actually have a slot.
         *
         * Not the vector's length, which includes other registries' empty slots.
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
         * @tparam Fn Callable taking Base&.
         * @param fn Called once per filled slot.
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
