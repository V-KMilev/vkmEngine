#pragma once

#include "ecs/entity.h"

#include "panels/inspector/cards.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;
struct EditorState;
struct EditorContext;

/**
 * @brief The selected entity's components as cards, in VKM_SCENE_COMPONENTS order, or the World's.
 *
 * Edits are undoable, and inside a prefab instance they are overrides (PrefabOverrides). Cards name
 * authoring mistakes in place: docs/reference/editor.md, "A card names what its component is waiting for".
 */
class InspectorPanel {
    public:
        InspectorPanel() = default;
        ~InspectorPanel() = default;

        InspectorPanel(const InspectorPanel& other) = delete;
        InspectorPanel& operator=(const InspectorPanel& other) = delete;

        InspectorPanel(InspectorPanel && other) = delete;
        InspectorPanel& operator=(InspectorPanel && other) = delete;

    public:
        /**
         * @brief Draw the selection's cards, or the World's.
         *
         * @param ec The frame's editor context.
         */
        void draw(EditorContext& ec);

    private:
        void drawIdentityHeader(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id);
        void drawPrefabSection(EditorContext& ec, EntityId id);

        /// Add Component's popup, opened by the header's Add.
        void drawAddComponentMenu(Scene& scene, EditorState& state, EntityId id);

    private:
        CardMemory m_memory;
        EntityId   m_shown;                     ///< Whose cards the last frame drew; none for the World's.
        char       m_componentFilter[48] = {};  ///< Add Component's search.
};

} // namespace Vkm::Engine
