#pragma once

#include <memory>

#include "ecs/entity.h"

namespace Engine {

class Scene;
class ResourceManager;
class BehaviorSystem;
class BehaviorFieldVisitor;

/**
 * @brief Base class for native C++ gameplay behaviors.
 *
 * The engine's MonoBehaviour / ActorComponent analogue: subclass it, override
 * the lifecycle hooks, and attach instances to an entity through a
 * ScriptComponent. BehaviorSystem drives the hooks during play mode and injects
 * the engine context (entity, scene, resources) before onStart(), so hooks
 * reach the rest of the engine via m_scene / m_resources / m_entity.
 *
 * Non-copyable and non-movable: instances are owned by unique_ptr inside
 * ScriptComponent. Deep-copy for entity duplication goes through clone().
 */
class Behavior {
    public:
        Behavior() = default;
        virtual ~Behavior() = default;

        Behavior(const Behavior& other) = delete;
        Behavior& operator=(const Behavior& other) = delete;

        Behavior(Behavior && other) = delete;
        Behavior& operator=(Behavior && other) = delete;

    public:
        virtual void onStart()             {}  ///< First tick this instance runs in play mode.
        virtual void onUpdate(float dt)    {}  ///< Variable step; dt is simulation seconds.
        virtual void onFixedUpdate(float dt) {}  ///< Fixed step; dt = fixedDeltaTime. Opt-in.
        virtual void onDestroy()           {}  ///< Instance torn down (entity removed / play stopped / shutdown).

        /**
         * @brief Stable type name, identical to this type's BehaviorRegistry key.
         *
         * Single source of truth shared with registration: a subclass declares
         * `static constexpr const char* TYPE_NAME` and returns it here, and
         * BehaviorRegistry::registerBehavior<T>() keys off the same constant.
         * Serialization round-trips the behavior by this name.
         */
        virtual const char* typeName() const = 0;

        /**
         * @brief Visit the behavior's reflected authoring fields.
         *
         * The editor inspector and the serializer use this to read/write fields
         * through a `Behavior*` without knowing the concrete type. Default does
         * nothing; ReflectedBehavior generates it from the VKM_REFLECT markup.
         */
        virtual void visitFields(BehaviorFieldVisitor& visitor) {}

        /**
         * @brief Deep copy for entity duplication.
         *
         * Copy only authored fields; the engine context (m_entity/m_scene/
         * m_resources) and the started flag are rebound on the new instance by
         * BehaviorSystem.
         */
        virtual std::unique_ptr<Behavior> clone() const = 0;

    protected:
        EntityId         m_entity{};
        Scene*           m_scene     = nullptr;
        ResourceManager* m_resources = nullptr;

    private:
        friend class BehaviorSystem;

        /// Injected by BehaviorSystem before onStart so hooks can reach the engine.
        void bindContext(EntityId entity, Scene& scene, ResourceManager& resources) {
            m_entity    = entity;
            m_scene     = &scene;
            m_resources = &resources;
        }

        bool m_started  = false;  ///< onStart already fired (managed by BehaviorSystem).
        bool m_disabled = false;  ///< Set after a hook threw; BehaviorSystem then skips it.
};

} // namespace Engine
