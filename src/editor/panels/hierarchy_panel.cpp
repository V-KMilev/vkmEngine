#include "panels/hierarchy_panel.h"
#include "framework/editor_common.h"
#include "input/editor_actions.h"

namespace Engine {

namespace {
    /// True if @p maybeAncestor is @p node itself or anywhere up its parent
    /// chain. Used to reject drag-reparenting that would create a cycle.
    bool isSelfOrAncestor(const Scene& scene, EntityId node, EntityId maybeAncestor) {
        EntityId cur = node;
        for (int guard = 0; cur && guard < 64; ++guard) {
            if (cur == maybeAncestor) return true;
            if (!scene.has<Hierarchy>(cur)) break;
            cur = scene.get<Hierarchy>(cur).parent;
        }
        return false;
    }
}

void HierarchyPanel::draw(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;
    auto& scene = ctx.scene;

    // Panel header
    ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::HEADER_TEXT);
    ImGui::TextUnformatted("Hierarchy");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    float btnW = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btnW - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##Filter", "Search...", m_filter, sizeof(m_filter));
    ImGui::SameLine();
    if (ImGui::Button("+", ImVec2(btnW, 0))) {
        ImGui::OpenPopup("##CreatePopup");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create Entity");
    if (ImGui::BeginPopup("##CreatePopup")) {
        EditorActions::drawCreateEntityMenu(scene, ctx.resources, state);
        ImGui::EndPopup();
    }
    ImGui::Spacing();

    bool hasFilter = m_filter[0] != '\0';

    if (ImGui::BeginChild("##Tree", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()))) {
        // Rebuild root list only when entities change (not every frame).
        size_t currentCount = scene.entityCount();
        bool wasDirty = state.hierarchyDirty || currentCount != m_lastEntityCount;
        if (wasDirty) {
            m_cachedRoots.clear();
            m_cachedRoots.reserve(currentCount);
            scene.forEach<Transform>([&](EntityId id, const Transform&) {
                bool isRoot = !scene.has<Hierarchy>(id) || !scene.get<Hierarchy>(id).parent;
                if (isRoot) m_cachedRoots.push_back(id);
            });
            m_lastEntityCount = currentCount;
            state.hierarchyDirty = false;
        }

        const auto& displayList = hasFilter ? m_cachedFiltered : m_cachedRoots;

        if (hasFilter) {
            // Only rebuild filtered list when filter text or entity count changes
            bool filterChanged = std::strcmp(m_filter, m_lastFilter) != 0;
            if (filterChanged || wasDirty) {
                std::strncpy(m_lastFilter, m_filter, sizeof(m_lastFilter) - 1);
                m_lastFilter[sizeof(m_lastFilter) - 1] = '\0';
                m_cachedFiltered.clear();
                // Search all entities (not just roots) so children are discoverable
                scene.forEach<Transform>([&](EntityId id, const Transform&) {
                    char name[64];
                    getEntityDisplayName(scene, id, name, sizeof(name));
                    if (matchesFilter(name, m_filter))
                        m_cachedFiltered.push_back(id);
                });
            }
        }

        // ImGuiListClipper: only build/draw the rows actually on screen.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(displayList.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                EntityId id = displayList[i];
                if (hasFilter) {
                    ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                         | ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (state.selectedEntity == id) f |= ImGuiTreeNodeFlags_Selected;
                    char icon[8], name[64];
                    getEntityIcon(scene, id, icon, sizeof(icon));
                    getEntityDisplayName(scene, id, name, sizeof(name));
                    ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(id.index)),
                                     f, "%s  %s", icon, name);
                    if (ImGui::IsItemClicked()) state.selectedEntity = id;
                    drawEntityContextMenu(scene, state, id);
                } else {
                    drawEntityNode(scene, state, id);
                }
            }
        }

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            && !ImGui::IsAnyItemHovered()) {
            state.selectedEntity = {};
        }

        // Right-click on empty space
        if (ImGui::BeginPopupContextWindow("##HierarchyCtx", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight)) {
            EditorActions::drawCreateEntityMenu(scene, ctx.resources, state);
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();

    ImGui::TextDisabled("%zu entities", scene.entityCount());
}

void HierarchyPanel::drawEntityNode(Scene& scene, EditorState& state, EntityId entity) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_SpanAvailWidth
                             | ImGuiTreeNodeFlags_FramePadding;

    bool hasChildren = scene.has<Hierarchy>(entity) && scene.get<Hierarchy>(entity).firstChild;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (state.selectedEntity == entity) flags |= ImGuiTreeNodeFlags_Selected;

    char icon[8], name[64];
    getEntityIcon(scene, entity, icon, sizeof(icon));
    getEntityDisplayName(scene, entity, name, sizeof(name));
    bool nodeOpen = ImGui::TreeNodeEx(
        reinterpret_cast<void*>(static_cast<uintptr_t>(entity.index)),
        flags, "%s  %s", icon, name);

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload("VKM_ENTITY", &entity, sizeof(EntityId));
        ImGui::Text("Reparent %s", name);
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("VKM_ENTITY")) {
            EntityId dragged = *static_cast<const EntityId*>(pl->Data);
            // Reject dropping onto self or onto one of the dragged node's
            // own descendants (would create a hierarchy cycle).
            if (scene.isAlive(dragged) && !isSelfOrAncestor(scene, entity, dragged)) {
                HierarchyOperations::setParent(scene, dragged, entity);
                HierarchyOperations::markDirty(scene, dragged);
                state.hierarchyDirty = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) state.selectedEntity = entity;

    drawEntityContextMenu(scene, state, entity);

    if (nodeOpen && hasChildren) {
        HierarchyOperations::forEachChild(scene, entity, [&](EntityId child) {
            drawEntityNode(scene, state, child);
        });
        ImGui::TreePop();
    }
}

void HierarchyPanel::drawEntityContextMenu(Scene& scene, EditorState& state, EntityId entity) {
    if (!ImGui::BeginPopupContextItem()) return;

    char ctxName[64];
    getEntityDisplayName(scene, entity, ctxName, sizeof(ctxName));
    ImGui::TextDisabled("%s", ctxName);
    ImGui::Separator();

    if (ImGui::MenuItem("Select")) state.selectedEntity = entity;
    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) EditorActions::duplicateEntity(scene, state, entity);
    if (ImGui::MenuItem("Delete", "Del")) EditorActions::deleteEntity(scene, state, entity);

    if (scene.has<Hierarchy>(entity) && scene.get<Hierarchy>(entity).parent) {
        if (ImGui::MenuItem("Unparent")) {
            HierarchyOperations::removeFromParent(scene, entity);
            HierarchyOperations::markDirty(scene, entity);
            state.hierarchyDirty = true;
        }
    }

    if (scene.has<Transform>(entity)) {
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Transform")) {
            auto& t = scene.get<Transform>(entity);
            t.position = glm::vec3(0.0f);
            t.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            t.scale    = glm::vec3(1.0f);
            HierarchyOperations::markDirty(scene, entity);
        }
    }

    if (scene.has<Light>(entity)) {
        auto& light = scene.get<Light>(entity);
        if (ImGui::MenuItem(light.enabled ? "Disable Light" : "Enable Light")) {
            light.enabled = !light.enabled;
        }
    }

    if (scene.has<Mesh>(entity)) {
        auto& mesh = scene.get<Mesh>(entity);
        if (ImGui::MenuItem(mesh.visible ? "Hide" : "Show")) {
            mesh.visible = !mesh.visible;
        }
    }

    ImGui::EndPopup();
}

} // namespace Engine
