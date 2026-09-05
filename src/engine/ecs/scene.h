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
            return EntityId{m_entityAllocator.allocate()};
        }

        /**
         * @brief Allocate an entity at the requested slot index.
         *
         * Used by SceneSerializer to recreate saved entities with the same
         * slot indices they had on disk - that's what makes parent/child
         * indices (and editor selection mementos) directly valid after a
         * load, without any id-remap step.
         *
         * @param index The slot to claim; must not already hold a live entity.
         * @return The new entity, or a null id when that slot was taken. A
         *         caller that cannot rule that out has to check: writing
         *         components onto a null id is not a smaller failure than the
         *         double-owned slot this refuses to hand out.
         */
        EntityId createEntityAt(uint32_t index) {
            return EntityId{m_entityAllocator.allocateAt(index)};
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

            m_components.forEach([&](ISparseSet& set) { set.removeIfPresent(id.slot()); });
            m_entityAllocator.free(id.key);
        }

        bool isAlive(EntityId id)           const { return m_entityAllocator.has(id.key); }
        bool isAliveAtIndex(uint32_t index) const { return m_entityAllocator.isAliveAtIndex(index); }
        size_t entityCount()                const { return m_entityAllocator.size(); }

        /**
         * @brief The full id of the entity in slot @p index, generation included.
         *
         * The one way to turn a bare slot index - a SparseSet key, a serialized
         * parent link - back into an EntityId. Total, so an untrusted index can be
         * converted first and validated after: an index past the allocator's reach
         * yields the null id, and a recycled slot yields an id that fails isAlive().
         *
         * @param index Slot index; any value is accepted.
         * @return The entity id for that slot, null when the slot is out of reach.
         */
        EntityId entityAt(uint32_t index) const {
            return EntityId{m_entityAllocator.handleAt(index)};
        }

    public:
        /**
         * @brief Add a component to an entity.
         * @tparam T Component type (any type; storage is created on first use).
         * @return Reference to the added component in storage.
         */
        template<typename T>
        auto& add(EntityId entity, T && component) {
            VKM_ASSERT(isAlive(entity), "Scene::add called with dead/stale entity");
            using U = std::remove_cv_t<std::remove_reference_t<T>>;
            return getStorage<U>().add(entity.slot(), std::forward<T>(component));
        }

        /**
         * @brief Remove a component of type T from an entity.
         */
        template<typename T>
        void remove(EntityId entity) {
            VKM_ASSERT(isAlive(entity), "Scene::remove called with dead/stale entity");
            auto* store = findStorage<T>();
            if (store && store->contains(entity.slot())) {
                store->remove(entity.slot());
            }
        }

        /**
         * @brief Check if an entity has a component of type T.
         */
        template<typename T>
        bool has(EntityId entity) const {
            VKM_ASSERT(isAlive(entity), "Scene::has called with dead/stale entity");
            auto* store = findStorage<T>();
            return store && store->contains(entity.slot());
        }

        /**
         * @brief Get a mutable reference to an entity's component of type T.
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
         * `has<T>(id) ? &get<T>(id) : nullptr` written once. That form is two
         * sparse lookups where this is one, and it names the entity twice -
         * which is a line a caller can write with two different entities in it,
         * and one that gets copied.
         *
         * @tparam T Component type.
         * @param entity Entity to look on; must be alive.
         * @return Pointer to its component, or nullptr.
         */
        template<typename T>
        T* tryGet(EntityId entity) {
            VKM_ASSERT(isAlive(entity), "Scene::tryGet called with dead/stale entity");
            auto* store = findStorage<T>();
            if (!store || !store->contains(entity.slot())) return nullptr;
            return &store->get(entity.slot());
        }

        /// @copydoc tryGet()
        template<typename T>
        const T* tryGet(EntityId entity) const {
            VKM_ASSERT(isAlive(entity), "Scene::tryGet called with dead/stale entity");
            const auto* store = findStorage<T>();
            if (!store || !store->contains(entity.slot())) return nullptr;
            return &store->get(entity.slot());
        }

        /**
         * @brief Number of live components of type T.
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
         * The scene must not gain or lose a First while this runs - see
         * SparseSet::forEach for what a swap-and-pop does to a walk in
         * progress. Creating or destroying entities is the same thing by
         * another name. A system that has to mutate collects what it will
         * touch and acts on it after the walk.
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
            // O(types + entities) rather than the O(entities x types) walk-and-
            // destroy: on a total reset every entity goes away at once, so nothing
            // needs detachFromHierarchy's partial-deletion guard.
            m_components.forEach([](ISparseSet& set) { set.clear(); });
            m_entityAllocator.clear();
            m_environment = Environment{};
            m_physics     = PhysicsSettings{};
            ++m_epoch;
        }

        /**
         * @brief Compact every component SparseSet to reclaim wasted memory.
         *
         * Called by SceneSerializer after load: the staging build grows every
         * sparse array a key at a time, so each ends up holding the capacity a
         * geometric growth reserved rather than the capacity it uses.
         */
        void compact() {
            m_components.forEach([](ISparseSet& set) { set.compact(); });
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
            m_components.swap(other.m_components);
            swap(m_environment, other.m_environment);
            swap(m_physics, other.m_physics);
            ++m_epoch;
            ++other.m_epoch;
        }

        /**
         * @brief Identity of the world these entities belong to, bumped by
         * every clear() and swap().
         *
         * A replacement world reuses the slot indices and generations of the one
         * it replaced - the serializer rebuilds each entity at the index it was
         * saved at - so a cache keyed on an entity, or on a pose, cannot tell
         * the new world from the old one and keeps serving what it captured of
         * a scene that is gone. Anything holding such a capture must drop it
         * when this moves.
         */
        uint64_t epoch() const { return m_epoch; }

    public:
        /**
         * @brief The scene's lighting environment (skybox + IBL): scene-global,
         *        always present, round-trips with the scene.
         *
         * Read by RenderView each frame; edited via the editor's World inspector.
         */
        Environment& environment() { return m_environment; }
        const Environment& environment() const { return m_environment; }

        /**
         * @brief The scene's physics world parameters: scene-global, always
         *        present, round-trips with the scene.
         *
         * Read by PhysicsSystem once per fixed step; kept beside the Environment
         * rather than inside it (see PhysicsSettings).
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
            return static_cast<SparseSet<T>&>(m_components.ensure<T>(
                [] { return std::make_unique<SparseSet<T>>(); }));
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
            return static_cast<SparseSet<T>*>(m_components.find<T>());
        }

        template<typename T>
        const SparseSet<T>* findStorage() const {
            return static_cast<const SparseSet<T>*>(m_components.find<T>());
        }

    private:
        Environment     m_environment;
        PhysicsSettings m_physics;

        /**
         * @brief Bumped by every clear() and swap(); see epoch().
         */
        uint64_t m_epoch = 0;

        SlotAllocator m_entityAllocator;
        TypeRegistry<ISparseSet> m_components;
        std::vector<ISceneObserver*> m_observers;  ///< Non-owning; each notified on entity destroy.
};

/**
 * @brief The lowest-slot entity carrying @p First (and @p Rest) that @p pred accepts.
 *
 * "Which camera is the eye", "which light is the sun", "which listener are the
 * ears" are all this question, and a scene is allowed to answer it ambiguously
 * - two cameras can both be marked active. Something has to break the tie, and
 * it cannot be iteration order: a `SparseSet` is packed, so removing any *other*
 * entity of that type swaps the last element into the hole and reorders the
 * walk. The eye would change because an unrelated camera was deleted.
 *
 * The lowest slot is stable under that, is the order an author sees in the
 * hierarchy, and is the same tie-break the physics solver canonicalises on.
 *
 * @tparam First Component type iterated; put the rarest first as always.
 * @tparam Rest  Further components the entity must also carry.
 * @param scene The scene to search.
 * @param pred  Called with (First&, Rest&...); true accepts the candidate.
 * @return The winning entity, or a null id when nothing qualifies.
 */
template <typename First, typename... Rest, typename Pred>
EntityId findLowestSlot(const Scene& scene, Pred pred) {
    EntityId found{};
    scene.forEach<First, Rest...>(
        [&](EntityId id, const First& first, const Rest&... rest) {
            if (!pred(first, rest...)) return;
            if (!found || id.slot() < found.slot()) found = id;
        });
    return found;
}

} // namespace Vkm::Engine
