#pragma once

#include "component_storage.h"
#include "entity.h"

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
 * Component storage is specialized and type-safe for supported component types.
 * 
 * Typical usage involves creating entities and adding/removing/querying components
 * using the template API for each component type.
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
        Scene() : m_nextEntityId(0) {}
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
            return Entity{ ++m_nextEntityId };
        }

        /**
         * @brief Destroy an entity by removing all of its components.
         * @param entity The entity to destroy.
         */
         void destroyEntity(Entity entity) {
            m_transforms.remove(entity.getID());
            m_meshes.remove(entity.getID());
            m_cameras.remove(entity.getID());
            m_animations.remove(entity.getID());
            m_lights.remove(entity.getID());
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
            return getStorage<T>().add(entity.getID(), std::move(component));
        }

        /**
         * @brief Check if an entity has a component of type T.
         * @tparam T Component type.
         * @param entity The entity to query.
         * @return true if the component exists for the entity, false otherwise.
         */
        template<typename T>
        bool has(Entity entity) const {
            return getStorage<T>().has(entity.getID());
        }

        /**
         * @brief Get a mutable reference to an entity's component of type T.
         * @tparam T Component type.
         * @param entity The entity from which to get the component.
         * @return Reference to the component.
         * @throws (implementation-defined) if the component does not exist.
         */
        template<typename T>
        T& get(Entity entity) {
            return getStorage<T>().get(entity.getID());
        }

        /**
         * @brief Get a const reference to an entity's component of type T.
         * @tparam T Component type.
         * @param entity The entity from which to get the component.
         * @return Const reference to the component.
         * @throws (implementation-defined) if the component does not exist.
         */
        template<typename T>
        const T& get(Entity entity) const {
            return getStorage<T>().get(entity.getID());
        }

        /**
         * @brief Remove a component of type T from an entity.
         * @tparam T Component type.
         * @param entity The entity whose component will be removed.
         */
        template<typename T>
        void remove(Entity entity) {
            getStorage<T>().remove(entity.getID());
        }

        /**
         * @brief Get direct access to the storage for a component type.
         * @tparam T Component type.
         * @return Reference to the component storage.
         */
        template<typename T>
        ComponentStorage<T>& storage() { return getStorage<T>(); }

        /**
         * @brief Get const access to the storage for a component type.
         * @tparam T Component type.
         * @return Const reference to the component storage.
         */
        template<typename T>
        const ComponentStorage<T>& storage() const { return getStorage<T>(); }

    private:
        /**
         * @brief Get mutable storage for a specific supported component type.
         * @tparam T Component type.
         * @return Reference to the component storage for T.
         * @note Only enabled for supported types; causes a static assertion otherwise.
         */
        template<typename T>
        ComponentStorage<T>& getStorage() {
            if      constexpr (std::is_same_v<T, Transform>) return m_transforms;
            else if constexpr (std::is_same_v<T, Mesh>)      return m_meshes;
            else if constexpr (std::is_same_v<T, Camera>)    return m_cameras;
            else if constexpr (std::is_same_v<T, Animation>) return m_animations;
            else if constexpr (std::is_same_v<T, Light>)     return m_lights;
            else                                             VKM_ASSERT(false, "Component type T is not supported. Supported types: Transform, Mesh, Camera, Animation, Light");
        }

        /**
         * @brief Get const storage for a specific supported component type.
         * @tparam T Component type.
         * @return Const reference to the component storage for T.
         * @note Only enabled for supported types; causes a static assertion otherwise.
         */
        template<typename T>
        const ComponentStorage<T>& getStorage() const {
            if      constexpr (std::is_same_v<T, Transform>) return m_transforms;
            else if constexpr (std::is_same_v<T, Mesh>)      return m_meshes;
            else if constexpr (std::is_same_v<T, Camera>)    return m_cameras;
            else if constexpr (std::is_same_v<T, Animation>) return m_animations;
            else if constexpr (std::is_same_v<T, Light>)     return m_lights;
            else                                             VKM_ASSERT(false, "Component type T is not supported. Supported types: Transform, Mesh, Camera, Animation, Light");
        }

    private:
        EntityId m_nextEntityId;

        ComponentStorage<Transform> m_transforms;
        ComponentStorage<Mesh>      m_meshes;
        ComponentStorage<Camera>    m_cameras;
        ComponentStorage<Animation> m_animations;
        ComponentStorage<Light>     m_lights;
};

} // namespace Engine
