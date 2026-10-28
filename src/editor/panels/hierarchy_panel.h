#pragma once

#include <cstddef>
#include <unordered_set>
#include <vector>

#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;
struct EditorState;
struct EditorContext;
class SceneIOController;

/**
 * @brief Editor panel displaying the entity hierarchy tree.
 *
 * A search lists its matches flat. A row's context menu holds the per-entity operations.
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
        void draw(EditorContext& ec, SceneIOController& sceneIO);

    private:
        struct Row {
            EntityId entity;
            int      depth;
        };

    private:
        /**
         * @brief Draw one row of the tree: this entity, and nothing under it.
         *
         * Uses ImGuiTreeNodeFlags_NoTreePushOnOpen with a manual indent: one line per row, no TreePop.
         *
         * @param scene     Scene being edited.
         * @param resources Resolves asset handles for the rename step.
         * @param state     Selection, history and the dirty flag.
         * @param sceneIO   For the context menu.
         * @param entity    The row's entity.
         * @param depth     Ancestor count, for the indent.
         * @param found     A search's row: on its own, opening nothing.
         */
        void drawEntityNode(
            Scene& scene,
            ResourceManager& resources,
            EditorState& state,
            SceneIOController& sceneIO,
            EntityId entity,
            int depth,
            bool found = false
        );

        /**
         * @brief Refill @ref m_visible with the rows the tree would show.
         *
         * Pre-order from the roots, descending only into nodes in @ref m_open. Not used
         * during a search.
         *
         * @param scene Scene to walk.
         */
        void buildVisibleRows(const Scene& scene);

        /**
         * @brief Open every ancestor of a selection made elsewhere, and ask for its row in view.
         *
         * @param scene Holds the selection.
         * @param state Supplies the active entity.
         */
        void revealSelection(const Scene& scene, const EditorState& state);

        /// The rows drawn, in order: the filtered list or the tree.
        const std::vector<EntityId>& rowEntities();

        /**
         * @brief Answer a click on @p entity's row: Shift spans the rows from the active one, as a list does.
         *
         * @param state  Takes the selection.
         * @param entity The row clicked.
         */
        void clickRow(EditorState& state, EntityId entity);

        /**
         * @brief Make @p entity's row a drop target: its top and bottom edges put the dragged
         *        entity beside it, its middle under it.
         *
         * An asset dragged from the Assets panel is assigned to the entity instead
         * (AssetBrowserPanel::acceptAssetOn).
         *
         * @param scene     Holds both.
         * @param resources Holds a dragged asset.
         * @param state     Takes the step.
         * @param entity    The row's entity.
         */
        void dropOnRow(Scene& scene, ResourceManager& resources, EditorState& state, EntityId entity);

        void drawEntityContextMenu(
            Scene& scene,
            ResourceManager& resources,
            EditorState& state,
            SceneIOController& sceneIO,
            EntityId entity
        );

        /**
         * @brief The field a row being renamed is drawn as, in the tree or in a search's results.
         *
         * Enter names it, as one undo step; Escape, leaving the field, or an empty name gives up.
         *
         * @param scene     Holds the entity.
         * @param resources Its assets, for the edit's step.
         * @param state     Takes the step.
         * @param entity    The entity being renamed.
         */
        void drawRenameField(Scene& scene, ResourceManager& resources, EditorState& state, EntityId entity);

    private:
        char m_filter[64] = {};
        /// Per-frame scratch, kept for its capacity.
        std::vector<EntityId> m_roots;
        std::vector<EntityId> m_filtered;
        std::vector<Row>      m_visible;
        std::vector<Row>      m_stack;
        std::vector<EntityId> m_children;

        /**
         * @brief Entity slots whose children are shown, by @ref buildVisibleRows.
         *
         * Held here, not by ImGui: ImGuiListClipper needs the list flattened before drawing,
         * which needs the open state before ImGui knows it.
         */
        std::unordered_set<uint32_t> m_open;

        std::vector<EntityId> m_rowEntities;  ///< rowEntities's answer for the tree, rebuilt each call.

        EntityId m_revealed{};  ///< The active entity revealSelection last saw.
        EntityId m_scrollTo{};  ///< Brought into view when its row is drawn, then cleared.

        // Inline rename; a default EntityId means none in progress.
        EntityId m_renameTarget{};
        char     m_renameBuf[64] = {};
        bool     m_renameFocusNeeded = false;
};

} // namespace Vkm::Engine
