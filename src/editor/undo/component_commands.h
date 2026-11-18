#pragma once

#include <string>
#include <utility>

#include "ecs/entity.h"
#include "ecs/settings/environment.h"
#include "ecs/settings/physics_settings.h"
#include "io/scene/scene_components.h"

#include "undo/command.h"

namespace Vkm::Engine {

class Scene;
class CommandHost;
class ResourceManager;

/**
 * @brief Undoable edit of the Scene's Environment or PhysicsSettings, which no entity holds.
 *
 * Consecutive edits of one value merge, taking the newer label.
 *
 * @tparam T Environment or PhysicsSettings; instantiated at the bottom of this header.
 */
template <typename T>
class SceneValueEditCommand : public Command {
    public:
        SceneValueEditCommand(const T& before, const T& after, const char* label)
            : m_before(before)
            , m_after(after)
            , m_label(label)
        {}
        ~SceneValueEditCommand() override = default;

        SceneValueEditCommand(const SceneValueEditCommand& other) = delete;
        SceneValueEditCommand& operator=(const SceneValueEditCommand& other) = delete;

        SceneValueEditCommand(SceneValueEditCommand && other) = delete;
        SceneValueEditCommand& operator=(SceneValueEditCommand && other) = delete;

    public:
        void redo(Scene& scene, CommandHost&) override;
        void undo(Scene& scene, CommandHost&) override;
        const char* label() const override { return m_label; }
        bool tryMerge(Command& incoming) override;

    private:
        T           m_before;
        T           m_after;
        const char* m_label;
};

/// Undoable add of a T to an entity; redo skips one that already has a T.
template <typename T>
class AddComponentCommand : public Command {
    public:
        AddComponentCommand(EntityId e, T value, const char* label)
            : m_entity(e)
            , m_value(std::move(value))
            , m_label(label)
        {}
        ~AddComponentCommand() override = default;

        AddComponentCommand(const AddComponentCommand& other) = delete;
        AddComponentCommand& operator=(const AddComponentCommand& other) = delete;

        AddComponentCommand(AddComponentCommand && other) = delete;
        AddComponentCommand& operator=(AddComponentCommand && other) = delete;

    public:
        void redo(Scene&, CommandHost&) override;
        void undo(Scene&, CommandHost&) override;
        const char* label() const override { return m_label; }
        bool addresses(uint32_t slotIndex) const override { return m_entity.slot() == slotIndex; }

    private:
        EntityId    m_entity;
        T           m_value;
        const char* m_label;
};

/// Undoable removal of an entity's T; undo puts back the snapshot taken before it.
template <typename T>
class RemoveComponentCommand : public Command {
    public:
        RemoveComponentCommand(EntityId e, T snapshot, const char* label)
            : m_entity(e)
            , m_snapshot(std::move(snapshot))
            , m_label(label)
        {}
        ~RemoveComponentCommand() override = default;

        RemoveComponentCommand(const RemoveComponentCommand& other) = delete;
        RemoveComponentCommand& operator=(const RemoveComponentCommand& other) = delete;

        RemoveComponentCommand(RemoveComponentCommand && other) = delete;
        RemoveComponentCommand& operator=(RemoveComponentCommand && other) = delete;

    public:
        void redo(Scene&, CommandHost&) override;
        void undo(Scene&, CommandHost&) override;
        const char* label() const override { return m_label; }
        bool addresses(uint32_t slotIndex) const override { return m_entity.slot() == slotIndex; }

    private:
        EntityId    m_entity;
        T           m_snapshot;
        const char* m_label;
};

/// Undoable edit of a whole component's value; only for one with no cross-entity or re-bake effect.
template <typename T>
class ComponentEditCommand : public Command {
    public:
        ComponentEditCommand(EntityId e, const T& before, const T& after, const char* label)
            : m_entity(e)
            , m_before(before)
            , m_after(after)
            , m_label(label)
        {}
        ~ComponentEditCommand() override = default;

        ComponentEditCommand(const ComponentEditCommand& other) = delete;
        ComponentEditCommand& operator=(const ComponentEditCommand& other) = delete;

        ComponentEditCommand(ComponentEditCommand && other) = delete;
        ComponentEditCommand& operator=(ComponentEditCommand && other) = delete;

    public:
        void redo(Scene&, CommandHost&) override;
        void undo(Scene&, CommandHost&) override;
        const char* label() const override { return m_label; }
        bool tryMerge(Command& incoming) override;
        bool addresses(uint32_t slotIndex) const override { return m_entity.slot() == slotIndex; }

    private:
        EntityId    m_entity;
        T           m_before;
        T           m_after;
        const char* m_label;
};

/**
 * @brief Undoable edit of an entity's whole ScriptComponent, held as JSON: a behavior list is move-only.
 *
 * Add, edit and remove are one command over two strings, empty meaning no ScriptComponent. Merges
 * with the next script edit on the same entity inside one gesture.
 */
class ScriptEditCommand : public Command {
    public:
        ScriptEditCommand(EntityId e, std::string before, std::string after, const char* label)
            : m_entity(e)
            , m_before(std::move(before))
            , m_after(std::move(after))
            , m_label(label)
        {}
        ~ScriptEditCommand() override = default;

        ScriptEditCommand(const ScriptEditCommand& other) = delete;
        ScriptEditCommand& operator=(const ScriptEditCommand& other) = delete;

        ScriptEditCommand(ScriptEditCommand && other) = delete;
        ScriptEditCommand& operator=(ScriptEditCommand && other) = delete;

    public:
        void redo(Scene& scene, CommandHost& host) override;
        void undo(Scene& scene, CommandHost& host) override;
        const char* label() const override { return m_label; }
        bool tryMerge(Command& incoming) override;
        bool addresses(uint32_t slotIndex) const override { return m_entity.slot() == slotIndex; }

        /**
         * @brief The entity's ScriptComponent as JSON, or empty when it has none.
         *
         * @param scene Scene holding the entity.
         * @param id Entity to read.
         * @return The serialized component, or empty.
         */
        static std::string capture(const Scene& scene, EntityId id);

    private:
        /**
         * @brief Put @p json back on @p id, removing the component when empty.
         *
         * @param scene Scene holding the entity.
         * @param id Entity to write.
         * @param json A ScriptComponent document, or empty for no component.
         */
        static void restore(Scene& scene, EntityId id, const std::string& json);

    private:
        EntityId    m_entity;
        std::string m_before;
        std::string m_after;
        const char* m_label;
};

/**
 * @brief Make @p target the one active ("main") camera, as one undo step.
 *
 * Each camera whose flag moves goes through editStep, so one inside a prefab instance takes an override.
 *
 * @param scene     Scene holding the cameras.
 * @param resources Resolves asset handles to names, for an override.
 * @param host      Where the step is pushed and the dirty flag set.
 * @param target    Camera made active.
 */
void pushActiveCamera(Scene& scene, ResourceManager& resources, CommandHost& host, EntityId target);

// Instantiated in component_commands.cpp, so includers need no bodies.
#define VKM_EDITOR_EXTERN_COMMAND(Type, Key)            \
    extern template class AddComponentCommand<Type>;    \
    extern template class RemoveComponentCommand<Type>; \
    extern template class ComponentEditCommand<Type>;
VKM_SCENE_VALUE_COMPONENTS(VKM_EDITOR_EXTERN_COMMAND, VKM_EDITOR_EXTERN_COMMAND, VKM_EDITOR_EXTERN_COMMAND)
#undef VKM_EDITOR_EXTERN_COMMAND

extern template class SceneValueEditCommand<Environment>;
extern template class SceneValueEditCommand<PhysicsSettings>;

} // namespace Vkm::Engine
