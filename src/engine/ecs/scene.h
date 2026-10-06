#pragma once

#include <cstdint>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "l_assert.h"

#include "ecs/component/core/hierarchy.h"
#include "core/memory/type_registry.h"
#include "ecs/environment.h"
#include "ecs/physics_settings.h"
#include "ecs/entity.h"
#include "ecs/scene_observer.h"
#include "core/memory/slot_allocator.h"
#include "core/memory/sparse_set.h"
#include "core/memory/types.h"

namespace Vkm::Engine {

/**
 * @brief Central registry managing entities and an open set of component types.
 *
 * Entities come from a SlotAllocator; components live in SparseSet<T>s created
 * on first use, so any type can be a component.
 */
class Scene {
    public:
        Scene() = default;
        ~Scene() = default;

        Scene(const Scene& other) = delete;
        Scene& operator=(const Scene& other) = delete;

        Scene(Scene && other) = delete;
        Scene& operator=(Scene && other) = delete;

    public:
        /**
         * @brief Create a new entity and assign a unique EntityId.
         *
         * A recycled slot is emptied first: add() only asserts against a dead
         * id, so without asserts a stale write leaves a component on the free
         * slot, which the next entity there would inherit.
         *
         * @return The created entity's id.
         */
        EntityId createEntity() {
            bool recycled = false;
            const EntityId id{m_entityAllocator.allocate(&recycled)};
            if (recycled) {
                m_components.forEach([&](ISparseSet& set) { set.removeIfPresent(id.slot()); });
            }
            return id;
        }

        /**
         * @brief Allocate an entity at the requested slot index.
         *
         * For recreating an entity at a recorded slot (a scene file, an undo
         * step, a peer) with no id remap. Empties the slot as createEntity() does.
         *
         * @param index Slot to claim; must not hold a live entity.
         * @return The new entity, or null when the slot was taken, is 0, or lies
         *         past SlotAllocator::MAX_CLAIMED_INDEX.
         */
        EntityId createEntityAt(uint32_t index) {
            const EntityId id{m_entityAllocator.allocateAt(index)};
            if (id) {
                m_components.forEach([&](ISparseSet& set) { set.removeIfPresent(id.slot()); });
            }
            return id;
        }

        /**
         * @brief Destroy an entity by removing all of its components and recycling its slot.
         *
         * The teardown is keyed on the bare slot, so a stale id asserts and is
         * refused, or it would destroy the slot's new owner.
         *
         * @param id Entity to destroy.
         */
        void destroyEntity(EntityId id) {
            VKM_ASSERT(isAlive(id), "Scene::destroyEntity called with dead/stale entity");
            if (!isAlive(id)) return;

            // Before tear-down, while the entity and its components are still intact.
            for (ISceneObserver* observer : m_observers) {
                observer->onEntityDestroyed(id);
            }
            detachFromHierarchy(*this, id);

            m_components.forEach([&](ISparseSet& set) { set.removeIfPresent(id.slot()); });
            m_entityAllocator.free(id.key);
        }

        bool isAlive(EntityId id)           const { return m_entityAllocator.has(id.key); }
        bool isAliveAtIndex(uint32_t index) const { return m_entityAllocator.isAliveAtIndex(index); }
        size_t entityCount()                const { return m_entityAllocator.size(); }

        /**
         * @brief The full id of the entity in slot @p index, generation included.
         *
         * Total: a dead slot yields an id that fails isAlive().
         *
         * @param index Slot index; any value is accepted.
         * @return The entity id for that slot, null when the slot is out of reach.
         */
        EntityId entityAt(uint32_t index) const {
            return EntityId{m_entityAllocator.handleAt(index)};
        }

    public:
        /**
         * @brief Give an entity a component it does not have yet.
         *
         * A second T asserts; without asserts the existing one is kept. To
         * overwrite, `remove<T>()` then add, or assign through `get<T>()`.
         *
         * @tparam T Component type; storage is created on first use.
         * @param entity Entity to add to; alive, without a T.
         * @param component Moved or copied in.
         * @return The component in storage.
         */
        template<typename T>
        auto& add(EntityId entity, T && component) {
            VKM_ASSERT(isAlive(entity), "Scene::add called with dead/stale entity");
            using U = std::remove_cv_t<std::remove_reference_t<T>>;
            VKM_ASSERT(!has<U>(entity), "Scene::add: entity already carries this component type");
            return getStorage<U>().add(entity.slot(), std::forward<T>(component));
        }

        /**
         * @brief Take a component of type T off an entity.
         *
         * A missing T is fine. A stale entity asserts and is refused, or the
         * removal would hit the slot's new owner.
         *
         * @tparam T Component type.
         * @param entity Entity to take it from; must be alive.
         */
        template<typename T>
        void remove(EntityId entity) {
            VKM_ASSERT(isAlive(entity), "Scene::remove called with dead/stale entity");
            if (!isAlive(entity)) return;
            auto* store = findStorage<T>();
            if (store && store->contains(entity.slot())) {
                store->remove(entity.slot());
            }
        }

        /**
         * @brief Check if an entity has a component of type T.
         *
         * @tparam T Component type.
         * @param entity Entity to look on; any value is accepted.
         * @return True when that entity is alive and carries a T.
         */
        template<typename T>
        bool has(EntityId entity) const {
            if (!isAlive(entity)) return false;
            auto* store = findStorage<T>();
            return store && store->contains(entity.slot());
        }

        /**
         * @brief Get a mutable reference to an entity's component of type T.
         *
         * @tparam T Component type.
         * @param entity Entity to read; must be alive and carry a T.
         * @return The component in storage.
         */
        template<typename T>
        T& get(EntityId entity) {
            VKM_ASSERT(isAlive(entity), "Scene::get called with dead/stale entity");
            auto* store = findStorage<T>();
            VKM_ASSERT(store, "Scene::get called for unregistered component type");
            return store->get(entity.slot());
        }

        /**
         * @brief Get a const reference to an entity's component of type T.
         *
         * @tparam T Component type.
         * @param entity Entity to read; must be alive and carry a T.
         * @return The component in storage.
         */
        template<typename T>
        const T& get(EntityId entity) const {
            VKM_ASSERT(isAlive(entity), "Scene::get called with dead/stale entity");
            auto* store = findStorage<T>();
            VKM_ASSERT(store, "Scene::get called for unregistered component type");
            return store->get(entity.slot());
        }

        /**
         * @brief The entity's T, or null when it has none.
         *
         * @tparam T Component type.
         * @param entity Entity to look on; any value is accepted.
         * @return Pointer to its component, or nullptr.
         */
        template<typename T>
        T* tryGet(EntityId entity) {
            if (!isAlive(entity)) return nullptr;
            auto* store = findStorage<T>();
            if (!store || !store->contains(entity.slot())) return nullptr;
            return &store->get(entity.slot());
        }

        /// @copydoc tryGet()
        template<typename T>
        const T* tryGet(EntityId entity) const {
            if (!isAlive(entity)) return nullptr;
            const auto* store = findStorage<T>();
            if (!store || !store->contains(entity.slot())) return nullptr;
            return &store->get(entity.slot());
        }

        /**
         * @brief Number of live components of type T.
         *
         * @tparam T Component type.
         * @return How many entities carry a T.
         */
        template<typename T>
        size_t count() const {
            auto* store = findStorage<T>();
            return store ? store->size() : 0;
        }

    public:
        /**
         * @brief Iterate all live components densely (no holes).
         *
         * Walks First, yielding entities that also carry every Rest; put the
         * rarest first. Do not add or remove a First, or create or destroy
         * entities, during the walk (see SparseSet::forEach); collect and act after.
         *
         * @tparam First Component type iterated.
         * @tparam Rest  Further required component types.
         * @param fn Callable as void(EntityId, First&, Rest&...).
         */
        template<typename First, typename... Rest, typename Fn>
        void forEach(Fn&& fn) {
            // findStorage: creating the set here would flip a later storage<T>()
            // from null, which a caller may branch on.
            auto* firstStorage = findStorage<First>();
            if (!firstStorage) return;

            if constexpr (sizeof...(Rest) == 0) {
                firstStorage->forEach([&](uint32_t entityIdx, First& first) {
                    EntityId eid = entityAt(entityIdx);
                    fn(eid, first);
                });
            } else {
                auto restStorages = std::make_tuple(findStorage<Rest>()...);
                if (!(std::get<SparseSet<Rest>*>(restStorages) && ...)) return;

                firstStorage->forEach([&](uint32_t entityIdx, First& first) {
                    if (!(std::get<SparseSet<Rest>*>(restStorages)->contains(entityIdx) && ...)) return;

                    EntityId eid = entityAt(entityIdx);
                    fn(eid, first, std::get<SparseSet<Rest>*>(restStorages)->get(entityIdx)...);
                });
            }
        }

        template<typename First, typename... Rest, typename Fn>
        void forEach(Fn&& fn) const {
            auto* firstStorage = findStorage<First>();
            if (!firstStorage) return;

            if constexpr (sizeof...(Rest) == 0) {
                firstStorage->forEach([&](uint32_t entityIdx, const First& first) {
                    EntityId eid = entityAt(entityIdx);
                    fn(eid, first);
                });
            } else {
                auto restStorages = std::make_tuple(findStorage<Rest>()...);
                if (!(std::get<const SparseSet<Rest>*>(restStorages) && ...)) return;

                firstStorage->forEach([&](uint32_t entityIdx, const First& first) {
                    if (!(std::get<const SparseSet<Rest>*>(restStorages)->contains(entityIdx) && ...)) return;

                    EntityId eid = entityAt(entityIdx);
                    fn(eid, first, std::get<const SparseSet<Rest>*>(restStorages)->get(entityIdx)...);
                });
            }
        }

        /**
         * @brief Invoke fn(EntityId) for every live entity in this scene.
         *
         * @tparam Fn Callable taking an EntityId.
         * @param fn Called once per live entity, ascending by slot.
         */
        template<typename Fn>
        void forEachEntity(Fn&& fn) const {
            m_entityAllocator.forEach([&](uint32_t idx) {
                fn(entityAt(idx));
            });
        }

    public:
        /**
         * @brief Drop every component set and reset the entity allocator, the
         *        environment and the physics settings in one pass.
         *
         * The sets are destroyed, not emptied: a set's vtable may live in the
         * gameplay module, which a project switch unloads next.
         */
        void clear() {
            // Every entity goes at once, so no detachFromHierarchy is needed.
            m_components.clear();
            m_entityAllocator.clear();
            m_environment = Environment{};
            m_physics     = PhysicsSettings{};
            ++m_epoch;
        }

        /**
         * @brief Compact every component SparseSet to reclaim wasted memory.
         */
        void compact() {
            m_components.forEach([](ISparseSet& set) { set.compact(); });
        }

        /**
         * @brief Swap internal state with another Scene.
         *
         * For committing a staging scene atomically. Re-fetch any storage<T>()
         * pointer held across it; it now names the other scene's set.
         *
         * @param other Scene to exchange state with.
         */
        void swap(Scene& other) noexcept {
            using std::swap;
            m_entityAllocator.swap(other.m_entityAllocator);
            m_components.swap(other.m_components);
            swap(m_environment, other.m_environment);
            swap(m_physics, other.m_physics);
            ++m_epoch;
            ++other.m_epoch;
        }

        /**
         * @brief Register an observer, notified at the start of every destroyEntity.
         *
         * Survives swap() and clear(); remove it before it is destroyed.
         *
         * @param observer Observer to notify; not owned.
         */
        void addObserver(ISceneObserver* observer) {
            m_observers.push_back(observer);
        }

        /**
         * @brief Unregister an observer added with addObserver.
         *
         * @param observer Observer to stop notifying; absent is a no-op.
         */
        void removeObserver(ISceneObserver* observer) {
            for (auto it = m_observers.begin(); it != m_observers.end(); ++it) {
                if (*it == observer) {
                    m_observers.erase(it);
                    return;
                }
            }
        }

    public:
        /**
         * @brief Direct access to the typed SparseSet for component type T.
         *
         * For index-based or parallel iteration.
         *
         * @tparam T Component type.
         * @return The storage, or nullptr if no entity has ever added a T.
         */
        template<typename T>
        SparseSet<T>* storage() { return findStorage<T>(); }

        template<typename T>
        const SparseSet<T>* storage() const { return findStorage<T>(); }

        /**
         * @brief Identity of the world these entities belong to, bumped by clear() and swap().
         *
         * A replacement world can reuse the old ids (createEntityAt), so a cache
         * keyed on an entity or a pose must drop its capture when this moves.
         *
         * @return The current epoch.
         */
        uint64_t epoch() const { return m_epoch; }

        /**
         * @brief The scene's lighting environment, saved with it.
         *
         * @return This scene's Environment.
         */
        Environment& environment() { return m_environment; }
        const Environment& environment() const { return m_environment; }

        /**
         * @brief The scene's physics world parameters, saved with it.
         *
         * @return This scene's PhysicsSettings.
         */
        PhysicsSettings& physics() { return m_physics; }
        const PhysicsSettings& physics() const { return m_physics; }

    private:
        /**
         * @brief Get or create the typed SparseSet for component type T.
         *
         * @tparam T Component type.
         * @return The storage, created if absent.
         */
        template<typename T>
        SparseSet<T>& getStorage() {
            ISparseSet& base = m_components.ensure<T>([] { return std::make_unique<SparseSet<T>>(); });
            return static_cast<SparseSet<T>&>(base);
        }

        /**
         * @brief Find the typed SparseSet for component type T, never creating it.
         *
         * @tparam T Component type.
         * @return The storage, or nullptr if no T has ever been added.
         */
        template<typename T>
        SparseSet<T>* findStorage() {
            return static_cast<SparseSet<T>*>(m_components.find<T>());
        }

        template<typename T>
        const SparseSet<T>* findStorage() const {
            return static_cast<const SparseSet<T>*>(m_components.find<T>());
        }

    private:
        Environment     m_environment;
        PhysicsSettings m_physics;

        uint64_t m_epoch = 0;

        SlotAllocator m_entityAllocator;
        TypeRegistry<ISparseSet> m_components;
        std::vector<ISceneObserver*> m_observers;  ///< Non-owning.
};

/**
 * @brief The lowest-slot entity carrying @p First (and @p Rest) that @p pred accepts.
 *
 * Breaks a tie (two active cameras) stably: iteration order is not, since a
 * `SparseSet` removal swaps the last element into the hole.
 *
 * @tparam First Component type iterated; put the rarest first.
 * @tparam Rest  Further components the entity must also carry.
 * @param scene Scene to search.
 * @param pred  Called with (First&, Rest&...); true accepts the candidate.
 * @return The winning entity, or a null id when nothing qualifies.
 */
template <typename First, typename... Rest, typename Pred>
EntityId findLowestSlot(const Scene& scene, Pred pred) {
    EntityId found{};
    scene.forEach<First, Rest...>([&](EntityId id, const First& first, const Rest&... rest) {
        if (!pred(first, rest...)) return;
        if (!found || id.slot() < found.slot()) found = id;
    });
    return found;
}

} // namespace Vkm::Engine
