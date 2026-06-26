#pragma once

#include <functional>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "l_assert.h"

#include "ecs/component/hierarchy.h"
#include "ecs/environment.h"
#include "ecs/entity.h"
#include "core/memory/slot_allocator.h"
#include "core/memory/sparse_set.h"
#include "core/memory/types.h"

namespace Engine {

/**
 * @brief Central registry managing entities and an open set of component types.
 *
 * Scene provides efficient creation, component assignment, lookup, and removal
 * for entities. Entity lifetime is managed by a SlotAllocator (generation-safe
 * handles with recycling). Component data is stored in type-erased SparseSet<T>
 * containers that are created on first use - any type can be a component without
 * modifying Scene.
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
         * @return The created Entity.
         */
        Entity createEntity() {
            StorageIndex id = m_entityAllocator.allocate();
            return Entity{id};
        }

        /**
         * @brief Allocate an entity at the requested slot index.
         *
         * Used by SceneSerializer to recreate saved entities with the same
         * slot indices they had on disk - that's what makes parent/child
         * indices (and editor selection mementos) directly valid after a
         * load, without any id-remap step.
         */
        Entity createEntityAt(uint32_t index) {
            StorageIndex id = m_entityAllocator.allocateAt(index);
            return Entity{id};
        }

        /**
         * @brief Check if an entity is still alive (valid index + matching generation).
         * @param entity The entity to check.
         * @return true if the entity is alive.
         */
        bool isAlive(Entity entity) const { return isAlive(entity.getID()); }
        bool isAlive(EntityId id) const { return m_entityAllocator.has(id); }

        /**
         * @brief Bounds-tolerant check: is the slot at `index` currently
         * holding a live entity? Use this when you only have a raw slot
         * index from a previous frame/scene (e.g. a saved selection).
         */
        bool isAliveAtIndex(uint32_t index) const { return m_entityAllocator.isAliveAtIndex(index); }

        /**
         * @brief Number of live entities.
         */
        size_t entityCount() const { return m_entityAllocator.size(); }

        /**
         * @brief The scene's lighting environment (skybox + IBL): scene-global, always
         * present, round-trips with the scene. Read by RenderView each frame;
         * edited via the editor's World inspector.
         */
        Environment& environment() { return m_environment; }
        const Environment& environment() const { return m_environment; }

        /**
         * @brief Get the generation counter for an entity index (for reconstructing EntityId from dense index).
         */
        uint32_t generationOf(uint32_t index) const { return m_entityAllocator.generationOf(index); }

        /**
         * @brief Destroy an entity by removing all of its components and recycling its slot.
         * @param entity The entity to destroy.
         */
        void destroyEntity(Entity entity) {
            EntityId id = entity.getID();
            // Generic per-entity destroy notification (entity + components still
            // intact). Scene stays type-agnostic; a system installs the hook -
            // BehaviorSystem uses it to fire script onDestroy on every destroy
            // path. Single observer is all any current caller needs.
            if (m_onEntityDestroy) m_onEntityDestroy(id);
            detachFromHierarchy(*this, id);

            for (auto& set : m_components) {
                if (set && set->has(id.index)) {
                    set->remove(id.index);
                }
            }
            m_entityAllocator.free(id);
        }

        /**
         * @brief Add a component to an entity.
         * @tparam T Component type (any type; storage is created on first use).
         * @param entity The entity to add the component to.
         * @param component The component instance to add.
         * @return Reference to the added component in storage.
         */
        template<typename T>
        auto& add(Entity entity, T && component) {
            VKM_ASSERT(isAlive(entity), "Scene::add called with dead/stale entity");
            using U = std::remove_cv_t<std::remove_reference_t<T>>;
            return getStorage<U>().add(entity.getID().index, std::forward<T>(component));
        }

        /**
         * @brief Check if an entity has a component of type T.
         * @tparam T Component type.
         * @param entity Entity or EntityId to query.
         * @return true if the component exists for the entity, false otherwise.
         */
        template<typename T>
        bool has(Entity entity) const { return has<T>(entity.getID()); }

        template<typename T>
        bool has(EntityId entity) const {
            VKM_ASSERT(isAlive(entity), "Scene::has called with dead/stale entity");
            auto* store = findStorage<T>();
            return store && store->contains(entity.index);
        }

        /**
         * @brief Get a mutable reference to an entity's component of type T.
         * @tparam T Component type.
         * @param entity Entity or EntityId from which to get the component.
         * @return Reference to the component.
         */
        template<typename T>
        T& get(Entity entity) { return get<T>(entity.getID()); }

        template<typename T>
        T& get(EntityId entity) {
            VKM_ASSERT(isAlive(entity), "Scene::get called with dead/stale entity");
            return getStorage<T>().get(entity.index);
        }

        /**
         * @brief Get a const reference to an entity's component of type T.
         * @tparam T Component type.
         * @param entity Entity or EntityId from which to get the component.
         * @return Const reference to the component.
         */
        template<typename T>
        const T& get(Entity entity) const { return get<T>(entity.getID()); }

        template<typename T>
        const T& get(EntityId entity) const {
            VKM_ASSERT(isAlive(entity), "Scene::get called with dead/stale entity");
            auto* store = findStorage<T>();
            VKM_ASSERT(store, "Scene::get called for unregistered component type");
            return store->get(entity.index);
        }

        /**
         * @brief Remove a component of type T from an entity.
         * @tparam T Component type.
         * @param entity The entity whose component will be removed.
         */
        template<typename T>
        void remove(Entity entity) {
            VKM_ASSERT(isAlive(entity), "Scene::remove called with dead/stale entity");
            auto* store = findStorage<T>();
            if (store && store->contains(entity.getID().index)) {
                store->remove(entity.getID().index);
            }
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
            auto& firstStorage = getStorage<First>();

            if constexpr (sizeof...(Rest) == 0) {
                firstStorage.forEach([&](uint32_t entityIdx, First& first) {
                    EntityId eid{entityIdx, m_entityAllocator.generationOf(entityIdx)};
                    fn(eid, first);
                });
            } else {
                auto restStorages = std::make_tuple(&getStorage<Rest>()...);

                firstStorage.forEach([&](uint32_t entityIdx, First& first) {
                    if (!(std::get<SparseSet<Rest>*>(restStorages)->contains(entityIdx) && ...)) return;

                    EntityId eid{entityIdx, m_entityAllocator.generationOf(entityIdx)};
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
                    EntityId eid{entityIdx, m_entityAllocator.generationOf(entityIdx)};
                    fn(eid, first);
                });
            } else {
                auto restStorages = std::make_tuple(findStorage<Rest>()...);
                if (!(std::get<const SparseSet<Rest>*>(restStorages) && ...)) return;

                firstStorage->forEach([&](uint32_t entityIdx, const First& first) {
                    if (!(std::get<const SparseSet<Rest>*>(restStorages)->contains(entityIdx) && ...)) return;

                    EntityId eid{entityIdx, m_entityAllocator.generationOf(entityIdx)};
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
                fn(EntityId{idx, m_entityAllocator.generationOf(idx)});
            });
        }

        /**
         * @brief Drop every component set and reset the entity allocator and
         * environment in one pass. Used for scene load to start from a clean
         * slate.
         */
        void clear() {
            // O(types + entities) rather than the previous
            // O(entities x types) walk-and-destroy. detachFromHierarchy's
            // dangling-pointer guard exists for partial deletion (sibling
            // links pointing at already-freed entities); on a total reset
            // every entity is going away in a single tear-down, so per-set
            // clear() + entity-allocator reset gives the same final state
            // without paying the cross-product cost.
            for (auto& set : m_components) {
                if (set) set->clear();
            }
            m_entityAllocator.clear();
            m_environment = Environment{};
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
        }

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

        /**
         * @brief Install a single observer invoked (with the EntityId) at the
         * start of every destroyEntity, before its components are removed.
         *
         * Keeps Scene type-agnostic while letting a system react to deletions -
         * BehaviorSystem uses it to fire script onDestroy on every destroy path.
         * Belongs to this Scene object, so it persists across swap()/clear()
         * (not swapped with scene contents).
         */
        void setOnEntityDestroy(std::function<void(EntityId)> callback) {
            m_onEntityDestroy = std::move(callback);
        }

    private:
        /** @brief Get or create the typed SparseSet for component type T. */
        template<typename T>
        SparseSet<T>& getStorage() {
            TypeId id = typeId<T>();
            if (id >= m_components.size())
                m_components.resize(id + 1);
            auto& ptr = m_components[id];
            if (!ptr) ptr = std::make_unique<SparseSet<T>>();
            return static_cast<SparseSet<T>&>(*ptr);
        }

        /** @brief Find the typed SparseSet for component type T, or nullptr if unregistered. */
        template<typename T>
        const SparseSet<T>* findStorage() const {
            TypeId id = typeId<T>();
            if (id >= m_components.size() || !m_components[id]) return nullptr;
            return static_cast<const SparseSet<T>*>(m_components[id].get());
        }

        /** @brief Non-const findStorage for mutable access without lazy creation. */
        template<typename T>
        SparseSet<T>* findStorage() {
            TypeId id = typeId<T>();
            if (id >= m_components.size() || !m_components[id]) return nullptr;
            return static_cast<SparseSet<T>*>(m_components[id].get());
        }

    private:
        SlotAllocator m_entityAllocator;
        std::vector<std::unique_ptr<ISparseSet>> m_components;
        Environment m_environment;

        /**
         * @brief Per-entity destroy observer (see setOnEntityDestroy). Not swapped:
         * it belongs to this Scene object, not the contents it holds.
         */
        std::function<void(EntityId)> m_onEntityDestroy;
};

} // namespace Engine
