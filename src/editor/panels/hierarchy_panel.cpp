#include "panels/hierarchy_panel.h"

#include <cfloat>
#include <cstring>

#include "framework/editor_common.h"
#include "framework/editor_commands.h"
#include "input/editor_actions.h"
#include "system/render/render_view.h"   // EnvironmentConfig (singleton row)

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

    drawPanelTitle("Scene");

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

    // Pinned scene-level entity. The Environment has no Transform so it never
    // appears in the entity tree below; this is its single, always-visible
    // entry point (Godot WorldEnvironment / Unreal World Settings pattern).
    {
        EntityId envId{};
        scene.forEach<EnvironmentConfig>([&](EntityId id, EnvironmentConfig&) {
            if (!envId) envId = id;
        });
        if (envId) {
            const bool sel = state.selectedEntity == envId;
            if (entitySelectable("##EnvRow", sel, EditorIcon::Environment, "Environment"))
                state.selectedEntity = envId;
        }
    }
    ImGui::Separator();
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
                    char name[64];
                    getEntityDisplayName(scene, id, name, sizeof(name));
                    entityTreeNode(reinterpret_cast<void*>(static_cast<uintptr_t>(id.index)),
                                   f, entityIconKind(scene, id), name);
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

    char name[64];
    getEntityDisplayName(scene, entity, name, sizeof(name));

    // Inline rename: when this entity is the rename target, replace the
    // tree-node label with an InputText. Commit on Enter / focus loss,
    // cancel on Escape. F2 / double-click on the row starts a session.
    if (m_renameTarget == entity) {
        ImGui::PushID(static_cast<int>(entity.index));
        if (m_renameFocusNeeded) {
            ImGui::SetKeyboardFocusHere();
            m_renameFocusNeeded = false;
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool committed = ImGui::InputText("##rename", m_renameBuf, sizeof(m_renameBuf),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        const bool lostFocus = ImGui::IsItemDeactivated() && !committed;
        const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);

        if (committed && m_renameBuf[0] != '\0') {
            if (!scene.has<Name>(entity)) scene.add(Entity{entity}, Name(m_renameBuf));
            else {
                auto& n = scene.get<Name>(entity);
                std::strncpy(n.value, m_renameBuf, sizeof(n.value) - 1);
                n.value[sizeof(n.value) - 1] = '\0';
            }
            state.markSceneDirty();
            m_renameTarget = {};
        } else if (cancelled || lostFocus) {
            m_renameTarget = {};
        }
        ImGui::PopID();
        return;  // don't draw the tree node this frame; recursion still works next frame
    }

    bool nodeOpen = entityTreeNode(
        reinterpret_cast<void*>(static_cast<uintptr_t>(entity.index)),
        flags, entityIconKind(scene, entity), name);

    // F2 or double-click on a selected, hovered row -> start rename.
    if (state.selectedEntity == entity && ImGui::IsItemHovered()
            && (ImGui::IsKeyPressed(ImGuiKey_F2)
             || ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) {
        m_renameTarget = entity;
        m_renameFocusNeeded = true;
        std::strncpy(m_renameBuf, name, sizeof(m_renameBuf) - 1);
        m_renameBuf[sizeof(m_renameBuf) - 1] = '\0';
    }

    // Hover tooltip: full name, id, and a component digest. Covers truncated
    // names in narrow panels and gives an at-a-glance summary.
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsItemToggledOpen()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(name);
        ImGui::TextDisabled("#%u", entity.index);
        ImGui::Separator();
        char comps[160] = {};
        size_t off = 0;
        auto append = [&](const char* s) {
            if (off >= sizeof(comps) - 1) return;
            const int n = snprintf(comps + off, sizeof(comps) - off,
                off ? "  %s" : "%s", s);
            if (n > 0) off += static_cast<size_t>(n);
        };
        if (scene.has<Transform>(entity)) append("Transform");
        if (scene.has<Mesh>(entity))      append("Mesh");
        if (scene.has<Light>(entity))     append("Light");
        if (scene.has<Camera>(entity))    append("Camera");
        if (scene.has<Animation>(entity)) append("Animation");
        if (scene.has<Hierarchy>(entity)) append("Hierarchy");
        ImGui::TextUnformatted(comps[0] ? comps : "(no components)");
        ImGui::EndTooltip();
    }

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
                EntityId oldParent{};
                if (scene.has<Hierarchy>(dragged)) {
                    oldParent = scene.get<Hierarchy>(dragged).parent;
                }
                HierarchyOperations::setParent(scene, dragged, entity);
                state.commands.push(std::make_unique<ReparentCommand>(
                    dragged, oldParent, entity, "Reparent Entity"));
                EditorActions::commitHierarchyMutation(scene, state, dragged);
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
            EditorActions::commitHierarchyMutation(scene, state, entity);
        }
    }

    if (scene.has<Transform>(entity)) {
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Transform")) {
            auto& t = scene.get<Transform>(entity);
            t.position = glm::vec3(0.0f);
            t.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            t.scale    = glm::vec3(1.0f);
            EditorActions::commitHierarchyMutation(scene, state, entity);
        }
    }

    if (scene.has<Light>(entity)) {
        auto& light = scene.get<Light>(entity);
        if (ImGui::MenuItem(light.enabled ? "Disable Light" : "Enable Light")) {
            light.enabled = !light.enabled;
            state.markSceneDirty();
        }
    }

    if (scene.has<Mesh>(entity)) {
        auto& mesh = scene.get<Mesh>(entity);
        if (ImGui::MenuItem(mesh.visible ? "Hide" : "Show")) {
            mesh.visible = !mesh.visible;
            state.markSceneDirty();
        }
    }

    if (scene.has<Camera>(entity)) {
        ImGui::Separator();
        const auto& cam = scene.get<Camera>(entity);
        const bool isActive = cam.active;
        if (ImGui::MenuItem("Look Through Camera", nullptr, false, !isActive)) {
            // Activate this camera; deactivate all others so the editor's
            // active-camera lookup picks this one on the next frame.
            scene.forEach<Camera>([&](EntityId other, Camera& c) {
                c.active = (other == entity);
            });
            state.markSceneDirty();
        }
    }

    ImGui::EndPopup();
}

} // namespace Engine
