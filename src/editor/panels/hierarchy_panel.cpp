#include "../editor_common.h"

namespace Engine {
void EditorSystem::drawHierarchyPanel(FrameContext& ctx) {
    auto& scene = ctx.scene;

    float btnW = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btnW - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##Filter", "Search...", m_hierarchyFilter, sizeof(m_hierarchyFilter));
    ImGui::SameLine();
    if (ImGui::Button("+", ImVec2(btnW, 0))) {
        ImGui::OpenPopup("##CreatePopup");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create Entity");
    if (ImGui::BeginPopup("##CreatePopup")) {
        drawCreateEntityMenu(scene, ctx.resources);
        ImGui::EndPopup();
    }
    ImGui::Spacing();

    bool hasFilter = m_hierarchyFilter[0] != '\0';

    if (ImGui::BeginChild("##Tree", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()))) {
        // Rebuild root list only when entities change (not every frame).
        size_t currentCount = scene.entityCount();
        if (m_hierarchyDirty || currentCount != m_lastEntityCount) {
            m_cachedRoots.clear();
            m_cachedRoots.reserve(currentCount);
            scene.forEach<Transform>([&](EntityId id, const Transform&) {
                bool isRoot = !scene.has<Hierarchy>(id) || !scene.get<Hierarchy>(id).parent;
                if (isRoot) m_cachedRoots.push_back(id);
            });
            m_lastEntityCount = currentCount;
            m_hierarchyDirty = false;
        }

        const auto& displayList = hasFilter ? m_cachedFiltered : m_cachedRoots;

        if (hasFilter) {
            m_cachedFiltered.clear();
            // Search all entities (not just roots) so children are discoverable
            scene.forEach<Transform>([&](EntityId id, const Transform&) {
                char name[64];
                getEntityDisplayName(scene, id, name, sizeof(name));
                if (matchesFilter(name, m_hierarchyFilter))
                    m_cachedFiltered.push_back(id);
            });
        }

        // ImGuiListClipper: only draws visible rows instead of all 13,000+.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(displayList.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                EntityId id = displayList[i];
                if (hasFilter) {
                    ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                         | ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (m_selectedEntity == id) f |= ImGuiTreeNodeFlags_Selected;
                    char icon[8], name[64];
                    getEntityIcon(scene, id, icon, sizeof(icon));
                    getEntityDisplayName(scene, id, name, sizeof(name));
                    ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(id.index)),
                                     f, "%s  %s", icon, name);
                    if (ImGui::IsItemClicked()) m_selectedEntity = id;
                    drawEntityContextMenu(scene, id);
                } else {
                    drawEntityNode(scene, id);
                }
            }
        }

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            && !ImGui::IsAnyItemHovered()) {
            m_selectedEntity = {};
        }

        // Right-click on empty space
        if (ImGui::BeginPopupContextWindow("##HierarchyCtx", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight)) {
            drawCreateEntityMenu(scene, ctx.resources);
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();

    ImGui::TextDisabled("%zu entities", scene.entityCount());
}

void EditorSystem::drawEntityNode(Scene& scene, EntityId entity) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_SpanAvailWidth
                             | ImGuiTreeNodeFlags_FramePadding;

    bool hasChildren = scene.has<Hierarchy>(entity) && scene.get<Hierarchy>(entity).firstChild;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (m_selectedEntity == entity) flags |= ImGuiTreeNodeFlags_Selected;

    char icon[8], name[64];
    getEntityIcon(scene, entity, icon, sizeof(icon));
    getEntityDisplayName(scene, entity, name, sizeof(name));
    bool nodeOpen = ImGui::TreeNodeEx(
        reinterpret_cast<void*>(static_cast<uintptr_t>(entity.index)),
        flags, "%s  %s", icon, name);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) m_selectedEntity = entity;

    drawEntityContextMenu(scene, entity);

    if (nodeOpen && hasChildren) {
        HierarchyUtils::forEachChild(scene, entity, [&](EntityId child) {
            drawEntityNode(scene, child);
        });
        ImGui::TreePop();
    }
}

void EditorSystem::drawEntityContextMenu(Scene& scene, EntityId entity) {
    if (!ImGui::BeginPopupContextItem()) return;

    char ctxName[64];
    getEntityDisplayName(scene, entity, ctxName, sizeof(ctxName));
    ImGui::TextDisabled("%s", ctxName);
    ImGui::Separator();

    if (ImGui::MenuItem("Select")) m_selectedEntity = entity;
    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) duplicateEntity(scene, entity);
    if (ImGui::MenuItem("Delete", "Del")) deleteEntity(scene, entity);

    if (scene.has<Transform>(entity)) {
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Transform")) {
            auto& t = scene.get<Transform>(entity);
            t.position = glm::vec3(0.0f);
            t.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            t.scale    = glm::vec3(1.0f);
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
