#pragma once

#include "entity.h"
#include "slot_allocator.h"
#include "sparse_set.h"

#include "transform.h"
#include "mesh.h"
#include "camera.h"
#include "animation.h"
#include "light.h"

namespace Engine {

/**
 * @brief The Scene class provides management of entities and their components within the ECS architecture.
 *
 * Scene acts as a central registry for all entities and a fixed set of component types,
 * providing efficient creation, component assignment, lookup, and removal for entities.
 * Entity lifetime is managed by a SlotAllocator (generation-safe handles with recycling).
 * Component data is stored in SparseSet<T> containers, keyed by entity sparse index.
 *
 * Currently, supported component types are:
 *   - Transform
 *   - Mesh
 *   - Camera
 *   - Animation
 *   - Light
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
         * @brief Destroy an entity by removing all of its components and recycling its slot.
         * @param entity The entity to destroy.
         */
        void destroyEntity(Entity entity) {
            EntityId id = entity.getID();
            removeAt<Transform>(id.index);
            removeAt<Mesh>(id.index);
            removeAt<Camera>(id.index);
            removeAt<Animation>(id.index);
            removeAt<Light>(id.index);
            m_entityAllocator.free(id);
        }

        /**
         * @brief Add a component to an entity.
         * @tparam T Component type (must be a supported type).
         * @param entity The entity to add the component to.
         * @param component The component instance to add (moves in).
         * @return Reference to the added component in storage.
         */
        template<typename T>
        T& add(Entity entity, T && component) {
            return getStorage<T>().add(entity.getID().index, std::move(component));
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
            return getStorage<T>().has(entity.index);
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
            return getStorage<T>().get(entity.index);
        }

        /**
         * @brief Remove a component of type T from an entity.
         * @tparam T Component type.
         * @param entity The entity whose component will be removed.
         */
        template<typename T>
        void remove(Entity entity) {
            removeAt<T>(entity.getID().index);
        }

        /**
         * @brief Number of live components of type T.
         * @tparam T Component type.
         */
        template<typename T>
        size_t count() const { return getStorage<T>().size(); }

        /**
         * @brief Iterate all live components of type T densely (no holes).
         *
         * Calls fn(EntityId, T&) for each live component in packed order.
         * EntityIds are reconstructed with the correct entity generation.
         *
         * @tparam T Component type.
         * @param fn Callable with signature void(EntityId, T&).
         */
        template<typename T, typename Fn>
        void forEach(Fn&& fn) {
            getStorage<T>().forEach([&](uint32_t entityIdx, T& component) {
                EntityId eid{entityIdx, m_entityAllocator.generationOf(entityIdx)};
                fn(eid, component);
            });
        }

        template<typename T, typename Fn>
        void forEach(Fn&& fn) const {
            getStorage<T>().forEach([&](uint32_t entityIdx, const T& component) {
                EntityId eid{entityIdx, m_entityAllocator.generationOf(entityIdx)};
                fn(eid, component);
            });
        }

    private:
        template<typename T>
        void removeAt(uint32_t index) {
            auto& store = getStorage<T>();
            if (store.has(index))
                store.remove(index);
        }

        template<typename T>
        SparseSet<T>& getStorage() {
            if      constexpr (std::is_same_v<T, Transform>) return m_transforms;
            else if constexpr (std::is_same_v<T, Mesh>)      return m_meshes;
            else if constexpr (std::is_same_v<T, Camera>)    return m_cameras;
            else if constexpr (std::is_same_v<T, Animation>) return m_animations;
            else if constexpr (std::is_same_v<T, Light>)     return m_lights;
            else                                             VKM_ASSERT(false, "Component type T is not supported. Supported types: Transform, Mesh, Camera, Animation, Light");
        }

        template<typename T>
        const SparseSet<T>& getStorage() const {
            if      constexpr (std::is_same_v<T, Transform>) return m_transforms;
            else if constexpr (std::is_same_v<T, Mesh>)      return m_meshes;
            else if constexpr (std::is_same_v<T, Camera>)    return m_cameras;
            else if constexpr (std::is_same_v<T, Animation>) return m_animations;
            else if constexpr (std::is_same_v<T, Light>)     return m_lights;
            else                                             VKM_ASSERT(false, "Component type T is not supported. Supported types: Transform, Mesh, Camera, Animation, Light");
        }

    private:
        SlotAllocator m_entityAllocator;

        SparseSet<Transform> m_transforms;
        SparseSet<Mesh>      m_meshes;
        SparseSet<Camera>    m_cameras;
        SparseSet<Animation> m_animations;
        SparseSet<Light>     m_lights;
};

} // namespace Engine
