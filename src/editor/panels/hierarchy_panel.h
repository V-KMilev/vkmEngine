#pragma once

#include <cstddef>
#include <vector>

#include "ecs/entity.h"

#include "../editor_panel.h"

namespace Engine {

class Scene;
struct EditorState;
struct EditorContext;

/**
 * @brief Editor panel displaying the entity hierarchy tree.
 *
 * Shows all root entities in a scrollable tree with search filtering,
 * entity selection, and context menus (create, duplicate, delete).
 * Owns its filter text and cached entity lists (rebuilt only when dirty).
 */
class HierarchyPanel : public EditorPanel {
    public:
        const char* panelId() const override { return "Hierarchy"; }
        void draw(EditorContext& ec) override;

    private:
        void drawEntityNode(Scene& scene, EditorState& state, EntityId entity);
        void drawEntityContextMenu(Scene& scene, EditorState& state, EntityId entity);

        char m_filter[64] = {};
        char m_lastFilter[64] = {};
        std::vector<EntityId> m_cachedRoots;
        std::vector<EntityId> m_cachedFiltered;
        size_t m_lastEntityCount = 0;
};

} // namespace Engine
