#pragma once

#include <cstddef>
#include <vector>

#include "ecs/entity.h"

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
class HierarchyPanel {
    public:
        HierarchyPanel() = default;
        ~HierarchyPanel() = default;

        HierarchyPanel(const HierarchyPanel& other) = delete;
        HierarchyPanel& operator=(const HierarchyPanel& other) = delete;

        HierarchyPanel(HierarchyPanel && other) = delete;
        HierarchyPanel& operator=(HierarchyPanel && other) = delete;

    public:
        void draw(EditorContext& ec);

    private:
        void drawEntityNode(Scene& scene, EditorState& state, EntityId entity);
        void drawEntityContextMenu(Scene& scene, EditorState& state, EntityId entity);

        char m_filter[64] = {};
        char m_lastFilter[64] = {};
        std::vector<EntityId> m_cachedRoots;
        std::vector<EntityId> m_cachedFiltered;
        size_t m_lastEntityCount = 0;

        // Inline-rename state. m_renameTarget == 0 (default-constructed
        // EntityId) means "no rename in progress". The buffer survives a
        // single rename session; cleared on commit/cancel.
        EntityId m_renameTarget{};
        char     m_renameBuf[64] = {};
        bool     m_renameFocusNeeded = false;
};

} // namespace Engine
