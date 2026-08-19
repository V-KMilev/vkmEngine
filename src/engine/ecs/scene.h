#pragma once

#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "l_assert.h"

#include "ecs/component/hierarchy.h"
#include "ecs/environment.h"
#include "ecs/entity.h"
#include "ecs/scene_observer.h"
#include "core/memory/slot_allocator.h"
#include "core/memory/sparse_set.h"
#include "core/memory/types.h"

namespace Vkm::Engine {

/**
 * @brief Central registry managing entities and an open set of component types.
 *
 * Entity lifetime is managed by a SlotAllocator (generation-safe handles with
 * recycling). Component data is stored in type-erased SparseSet<T> containers
 * that are created on first use - any type can be a component without modifying
 * Scene.
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
         * @return The created entity's id.
         */
        EntityId createEntity() {
            return m_entityAllocator.allocate();
        }

        /**
         * @brief Allocate an entity at the requested slot index.
         *
         * Used by SceneSerializer to recreate saved entities with the same
         * slot indices they had on disk - that's what makes parent/child
         * indices (and editor selection mementos) directly valid after a
         * load, without any id-remap step.
         */
        EntityId createEntityAt(uint32_t index) {
            return m_entityAllocator.allocateAt(index);
        }

        /**
         * @brief Destroy an entity by removing all of its components and recycling its slot.
         *
         * Every step below is keyed on the bare slot index, which a stale handle
         * still names correctly, so the generation has to be checked up front:
         * without it a recycled handle tears down whatever entity now holds the
         * slot. Destroying an already-dead entity is a no-op.
         *
         * @param id The entity to destroy.
         */
        void destroyEntity(EntityId id) {
            VKM_ASSERT(isAlive(id), "Scene::destroyEntity called with dead/stale entity");
            if (!isAlive(id)) return;

            // Notify observers before tear-down, while the entity and its
            // components are still intact.
            for (ISceneObserver* observer : m_observers) {
                observer->onEntityDestroyed(id);
            }
            detachFromHierarchy(*this, id);

            for (auto& set : m_components) {
                if (set) set->removeIfPresent(id.index);
            }
            m_entityAllocator.free(id);
        }

        bool isAlive(EntityId id)           const { return m_entityAllocator.has(id); }
        bool isAliveAtIndex(uint32_t index) const { return m_entityAllocator.isAliveAtIndex(index); }
        size_t entityCount()                const { return m_entityAllocator.size(); }

        /**
         * @brief The full id of the entity in slot @p index, generation included.
         *
         * The one way to turn a bare slot index - which is what a SparseSet key
         * and a serialized parent link are - back into an EntityId systems can
         * pass around.
         *
         * Total, so an untrusted index can be converted first and validated
         * after: an index past the allocator's reach yields the null id, and one
         * naming a slot that has since been recycled yields an id that fails
         * isAlive().
         *
         * @param index Slot index; any value is accepted.
         * @return The entity id for that slot, null when the slot is out of reach.
         */
        EntityId entityAt(uint32_t index) const {
            return m_entityAllocator.handleAt(index);
        }

    public:
        /**
         * @brief Add a component to an entity.
         * @tparam T Component type (any type; storage is created on first use).
         * @param entity The entity to add the component to.
         * @param component The component instance to add.
         * @return Reference to the added component in storage.
         */
        template<typename T>
        auto& add(EntityId entity, T && component) {
            VKM_ASSERT(isAlive(entity), "Scene::add called with dead/stale entity");
            using U = std::remove_cv_t<std::remove_reference_t<T>>;
            return getStorage<U>().add(entity.index, std::forward<T>(component));
        }

        /**
         * @brief Remove a component of type T from an entity.
         * @tparam T Component type.
         * @param entity The entity whose component will be removed.
         */
        template<typename T>
        void remove(EntityId entity) {
            VKM_ASSERT(isAlive(entity), "Scene::remove called with dead/stale entity");
            auto* store = findStorage<T>();
            if (store && store->contains(entity.index)) {
                store->remove(entity.index);
            }
        }

        /**
         * @brief Check if an entity has a component of type T.
         * @tparam T Component type.
         * @param entity The entity to query.
         * @return true if the component exists for the entity, false otherwise.
         */
        template<typename T>
        bool has(EntityId entity) const {
            VKM_ASSERT(isAlive(entity), "Scene::has called with dead/stale entity");
            auto* store = findStorage<T>();
            return store && store->contains(entity.index);
        }

        /**
         * @brief Get a mutable reference to an entity's component of type T.
         * @tparam T Component type.
         * @param entity The entity from which to get the component.
         * @return Reference to the component.
         */
        template<typename T>
        T& get(EntityId entity) {
            VKM_ASSERT(isAlive(entity), "Scene::get called with dead/stale entity");
            auto* store = findStorage<T>();
            VKM_ASSERT(store, "Scene::get called for unregistered component type");
            return store->get(entity.index);
        }

        /**
         * @brief Get a const reference to an entity's component of type T.
         * @tparam T Component type.
         * @param entity The entity from which to get the component.
         * @return Const reference to the component.
         */
        template<typename T>
        const T& get(EntityId entity) const {
            VKM_ASSERT(isAlive(entity), "Scene::get called with dead/stale entity");
            auto* store = findStorage<T>();
            VKM_ASSERT(store, "Scene::get called for unregistered component type");
            return store->get(entity.index);
        }

        /**
         * @brief Number of live components of type T.
         * @tparam T Component type.
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
         * With a single type, calls fn(EntityId, First&) for each live component.
         * With multiple types, iterates First and yields only entities that also
         * have all Rest types. Put the rarest component type first.
         *
         * @tparam First Primary component type (iterated).
         * @tparam Rest  Additional required component types (checked per entity).
         * @param fn Callable with signature void(EntityId, First&, Rest&...).
         */
        template<typename First, typename... Rest, typename Fn>
        void forEach(Fn&& fn) {
            // findStorage, not getStorage: iterating is a read, and creating the
            // set as a side effect of looking would also flip a later
            // storage<T>() from null to non-null - which several systems branch on.
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
         * Iteration order is ascending by slot index. Used by serialization
         * and editor flows that need to enumerate entities independent of
         * which components they happen to carry.
         */
        template<typename Fn>
        void forEachEntity(Fn&& fn) const {
            m_entityAllocator.forEach([&](uint32_t idx) {
                fn(entityAt(idx));
            });
        }

    public:
        /**
         * @brief Direct access to the typed SparseSet for component type T.
         *
         * Use for index-based / parallel iteration where Scene::get<T>(id)
         * per element would be wasteful. Returns nullptr if no entity has
         * ever added a T (the storage is lazy).
         *
         * @tparam T Component type.
         * @return Pointer to the SparseSet, or nullptr if unregistered.
         */
        template<typename T>
        SparseSet<T>* storage() { return findStorage<T>(); }

        template<typename T>
        const SparseSet<T>* storage() const { return findStorage<T>(); }

    public:
        /**
         * @brief Drop every component set and reset the entity allocator, the
         *        environment and the physics settings in one pass.
         *
         * Used for scene load to start from a clean slate. This is the only
         * definition of what a cleared scene starts from - callers replacing a
         * scene reset nothing themselves, or the two drift.
         */
        void clear() {
            // O(types + entities) rather than an O(entities x types)
            // walk-and-destroy. detachFromHierarchy's dangling-pointer guard is
            // there for partial deletion; on a total reset every entity goes away
            // in one tear-down, so per-set clear() + allocator reset reaches the
            // same final state without paying the cross-product cost.
            for (auto& set : m_components) {
                if (set) set->clear();
            }
            m_entityAllocator.clear();
            m_environment = Environment{};
            m_physics     = PhysicsSettings{};
        }

        /**
         * @brief Compact every component SparseSet to reclaim wasted memory.
         *
         * Called by SceneSerializer after load: the staging-then-swap path
         * can leave the sparse array oversized for the slots it now holds.
         */
        void compact() {
            for (auto& set : m_components) {
                if (set) set->compact();
            }
        }

        /**
         * @brief Swap internal state with another Scene.
         *
         * Used by SceneSerializer to commit a fully-loaded staging scene
         * atomically - either the load succeeds and the live scene is
         * replaced, or it fails and the live scene is left untouched.
         * Systems access storage via storage<T>() each frame (no cached
         * pointers across calls), so a swap between frames is safe.
         *
         * @param other Scene whose state to exchange with this.
         */
        void swap(Scene& other) noexcept {
            using std::swap;
            m_entityAllocator.swap(other.m_entityAllocator);  // non-movable, member swap
            swap(m_components, other.m_components);
            swap(m_environment, other.m_environment);
            swap(m_physics, other.m_physics);
        }

    public:
        /**
         * @brief The scene's lighting environment (skybox + IBL): scene-global, always present, round-trips with the scene.
         *
         * Read by RenderView each frame; edited via the editor's World inspector.
         */
        Environment& environment() { return m_environment; }
        const Environment& environment() const { return m_environment; }

        /**
         * @brief The scene's physics world parameters: scene-global, always
         *        present, round-trips with the scene.
         *
         * Beside the Environment rather than inside it: what a world is lit by
         * and what it falls at have nothing to do with each other. Read by
         * PhysicsSystem once per fixed step.
         */
        PhysicsSettings& physics() { return m_physics; }
        const PhysicsSettings& physics() const { return m_physics; }

        /**
         * @brief Register an observer, notified at the start of every destroyEntity
         * before components are removed.
         *
         * Observers are non-owning and belong to this Scene object, so they persist
         * across swap()/clear() (not swapped with scene contents); pair every
         * addObserver with removeObserver before the observer is destroyed.
         */
        void addObserver(ISceneObserver* observer) {
            m_observers.push_back(observer);
        }

        /**
         * @brief Unregister a previously added observer (no-op if not present).
         */
        void removeObserver(ISceneObserver* observer) {
            for (auto it = m_observers.begin(); it != m_observers.end(); ++it) {
                if (*it == observer) {
                    m_observers.erase(it);
                    return;
                }
            }
        }

    private:
        /**
         * @brief Get or create the typed SparseSet for component type T.
         *
         * @tparam T Component type whose storage is requested.
         * @return Reference to the storage for T (created if it did not exist).
         */
        template<typename T>
        SparseSet<T>& getStorage() {
            TypeId id = typeId<T>();
            if (id >= m_components.size())
                m_components.resize(id + 1);
            auto& ptr = m_components[id];
            if (!ptr) ptr = std::make_unique<SparseSet<T>>();
            return static_cast<SparseSet<T>&>(*ptr);
        }

        /**
         * @brief Find the typed SparseSet for component type T for mutable access.
         *
         * Hands back a mutable pointer but, unlike getStorage(), never creates
         * the storage.
         *
         * @tparam T Component type whose storage is requested.
         * @return Pointer to the storage for T, or nullptr if no T has ever been registered.
         */
        template<typename T>
        SparseSet<T>* findStorage() {
            TypeId id = typeId<T>();
            if (id >= m_components.size() || !m_components[id]) return nullptr;
            return static_cast<SparseSet<T>*>(m_components[id].get());
        }

        template<typename T>
        const SparseSet<T>* findStorage() const {
            TypeId id = typeId<T>();
            if (id >= m_components.size() || !m_components[id]) return nullptr;
            return static_cast<const SparseSet<T>*>(m_components[id].get());
        }

    private:
        Environment     m_environment;
        PhysicsSettings m_physics;

        SlotAllocator m_entityAllocator;
        std::vector<std::unique_ptr<ISparseSet>> m_components;
        std::vector<ISceneObserver*> m_observers;  ///< Non-owning; each notified on entity destroy.
};

} // namespace Vkm::Engine
