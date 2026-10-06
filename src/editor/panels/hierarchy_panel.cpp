#include "panels/hierarchy_panel.h"

#include <cfloat>
#include <cstring>
#include <utility>
#include <vector>

#include <imgui.h>
#include <glm/glm.hpp>

#include "session/scene_io_controller.h"
#include "command/component_edit.h"
#include "core/system.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/mesh.h"
#include "ecs/scene.h"
#include "ecs/hierarchy_operations.h"
#include "editor_context.h"
#include "editor_state.h"
#include "input/editor_keybinds.h"
#include "resource/resource_manager.h"
#include "ui/editor_icons.h"
#include "ui/editor_widgets.h"
#include "ui/editor_style.h"
#include "editor_actions.h"
#include "panels/inspector_panel.h"

namespace Vkm::Engine {

void HierarchyPanel::draw(EditorContext& ec, SceneIOController& sceneIO) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;
    auto& scene     = ctx.scene;
    auto& resources = ctx.resources;

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
        if (hasFilter) {
            // Every entity, not just the roots, so a child is discoverable.
            m_filtered.clear();
            scene.forEachEntity([&](EntityId id) {
                char name[64];
                getEntityDisplayName(scene, id, name, sizeof(name));
                if (matchesFilter(name, m_filter)) m_filtered.push_back(id);
            });
        }

        if (!hasFilter) buildVisibleRows(scene);
        const size_t rowCount = hasFilter ? m_filtered.size() : m_visible.size();

        // The World node: not an entity, the handle for scene-global settings.
        if (!hasFilter) {
            static char s_worldNodeId = 0;   // stable address -> a unique ImGui id for this non-entity row
            // FramePadding matches the entity rows' height, or the icon pokes past the top clip.
            ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
            if (state.worldSelected) f |= ImGuiTreeNodeFlags_Selected;
            entityTreeNode(static_cast<void*>(&s_worldNodeId), f, EditorIcon::SpaceWorld, "World");
            if (ImGui::IsItemClicked()) state.selectWorld();
            ImGui::Separator();
        }

        if (rowCount == 0 && !hasFilter) {
            ImGui::Spacing();
            ImGui::Spacing();
            const char* line = "The scene is empty.";
            const float lineW = ImGui::CalcTextSize(line).x;
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - lineW) * 0.5f);
            ImGui::TextDisabled("%s", line);
            const float createW = EditorStyle::px(170.0f);
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - createW) * 0.5f);
            if (ImGui::Button("+  Create Entity", ImVec2(createW, 0)))
                ImGui::OpenPopup("##CreateEmptyState");
            // Its own popup: the header's "##CreatePopup" is at panel scope and would not match here.
            if (ImGui::BeginPopup("##CreateEmptyState")) {
                EditorActions::drawCreateEntityMenu(scene, ec.frame.resources, state);
                ImGui::EndPopup();
            }
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rowCount));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                if (hasFilter) {
                    const EntityId id = m_filtered[static_cast<size_t>(i)];
                    ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                        | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
                    if (state.isSelected(id)) f |= ImGuiTreeNodeFlags_Selected;
                    char name[64];
                    getEntityDisplayName(scene, id, name, sizeof(name));
                    entityTreeNode(
                        reinterpret_cast<void*>(static_cast<uintptr_t>(id.slot())),
                        f,
                        entityIconKind(scene, id),
                        name
                    );
                    if (ImGui::IsItemClicked()) state.clickSelect(id);
                    drawEntityContextMenu(scene, resources, state, sceneIO, id);
                } else {
                    const Row& row = m_visible[static_cast<size_t>(i)];
                    drawEntityNode(scene, resources, state, sceneIO, row.entity, row.depth);
                }
            }
        }

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            && !ImGui::IsAnyItemHovered()) {
            state.deselect();
        }

        const bool menuOpen = ImGui::BeginPopupContextWindow(
            "##HierarchyCtx",
            ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight
        );
        if (menuOpen) {
            EditorActions::drawCreateEntityMenu(scene, ctx.resources, state);
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();

    ImGui::TextDisabled("%zu entities", scene.entityCount());
}

void HierarchyPanel::buildVisibleRows(const Scene& scene) {
    m_visible.clear();

    // Rebuilt every frame, so no scene change has to remember to invalidate it.
    m_roots.clear();
    m_roots.reserve(scene.entityCount());
    // Every entity: one with no Transform (a UI element, a lone Script) must still be selectable.
    scene.forEachEntity([&](EntityId id) {
        const Hierarchy* node = scene.tryGet<Hierarchy>(id);
        if (!node || !node->parent) m_roots.push_back(id);
    });

    // Iterative: depth is whatever a scene file says, and this stack costs one entry per level.
    m_stack.clear();
    for (size_t i = m_roots.size(); i-- > 0;) m_stack.push_back({m_roots[i], 0});

    while (!m_stack.empty()) {
        const Row row = m_stack.back();
        m_stack.pop_back();
        m_visible.push_back(row);

        if (m_open.count(row.entity.slot()) == 0) continue;

        // Pushed in reverse so they pop in hierarchy order.
        m_children.clear();
        HierarchyOperations::forEachChild(
            scene,
            row.entity,
            [&](EntityId child) { m_children.push_back(child); }
        );
        for (size_t i = m_children.size(); i-- > 0;) {
            m_stack.push_back({m_children[i], row.depth + 1});
        }
    }
}

void HierarchyPanel::drawEntityNode(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    SceneIOController& sceneIO,
    EntityId entity,
    int depth
) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_SpanAvailWidth
        | ImGuiTreeNodeFlags_FramePadding
        | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    const Hierarchy* node = scene.tryGet<Hierarchy>(entity);
    bool hasChildren = node && node->firstChild;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
    if (state.isSelected(entity)) flags |= ImGuiTreeNodeFlags_Selected;

    const float indent = static_cast<float>(depth) * ImGui::GetStyle().IndentSpacing;
    if (indent > 0.0f) ImGui::Indent(indent);
    struct IndentGuard {
        float amount;
        ~IndentGuard() { if (amount > 0.0f) ImGui::Unindent(amount); }
    } indentGuard{indent};

    char name[64];
    getEntityDisplayName(scene, entity, name, sizeof(name));

    if (m_renameTarget == entity) {
        ImGui::PushID(static_cast<int>(entity.slot()));
        if (m_renameFocusNeeded) {
            ImGui::SetKeyboardFocusHere();
            m_renameFocusNeeded = false;
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool committed = ImGui::InputText(
            "##rename",
            m_renameBuf,
            sizeof(m_renameBuf),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll
        );
        const bool lostFocus = ImGui::IsItemDeactivated() && !committed;
        const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);

        if (committed && m_renameBuf[0] != '\0') {
            if (!scene.tryGet<Name>(entity)) {
                // Undo removes the added Name, restoring the default display name.
                EditorActions::addComponent(scene, state, entity, makeName(m_renameBuf), "Name", "Rename");
            } else {
                EditScope<Name> n(scene, resources, state, entity, "Rename");
                *n = makeName(m_renameBuf);
            }
            m_renameTarget = {};
        } else if (cancelled || lostFocus) {
            m_renameTarget = {};
        }
        ImGui::PopID();
        return;  // the rename field replaces this row; its children are their own rows
    }

    // Forced from m_open, read back after: the arrow's click applies inside this call.
    if (hasChildren) ImGui::SetNextItemOpen(m_open.count(entity.slot()) != 0);

    const bool nodeOpen = entityTreeNode(
        reinterpret_cast<void*>(static_cast<uintptr_t>(entity.slot())),
        flags,
        entityIconKind(scene, entity),
        name
    );

    if (hasChildren) {
        if (nodeOpen) m_open.insert(entity.slot());
        else          m_open.erase(entity.slot());
    }

    if (state.selectedEntity == entity && ImGui::IsItemHovered()
        && (ImGui::IsKeyPressed(ImGuiKey_F2) || ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) {
        m_renameTarget = entity;
        m_renameFocusNeeded = true;
        std::strncpy(m_renameBuf, name, sizeof(m_renameBuf) - 1);
        m_renameBuf[sizeof(m_renameBuf) - 1] = '\0';
    }

    // Tooltip: the full name, for a narrow panel, plus a component digest.
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsItemToggledOpen()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(name);
        ImGui::TextDisabled("#%u", entity.slot());
        ImGui::Separator();
        // The Inspector's card list, in the Inspector's order.
        bool any = false;
#define VKM_HIERARCHY_DIGEST_ROW(Component, method, title, accent) \
        if (scene.has<Component>(entity)) {                         \
            ImGui::TextUnformatted(title);                          \
            any = true;                                             \
        }
        VKM_INSPECTOR_CARDS(VKM_HIERARCHY_DIGEST_ROW)
#undef VKM_HIERARCHY_DIGEST_ROW
        if (!any) ImGui::TextUnformatted("(no components)");
        ImGui::EndTooltip();
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload("VKM_ENTITY", &entity, sizeof(EntityId));
        ImGui::Text("Reparent %s", name);
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("VKM_ENTITY")) {
            const EntityId dragged = *static_cast<const EntityId*>(pl->Data);
            EditorActions::reparentKeepingWorld(scene, state, dragged, entity, "Reparent Entity");
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) state.clickSelect(entity);

    drawEntityContextMenu(scene, resources, state, sceneIO, entity);
}

void HierarchyPanel::drawEntityContextMenu(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    SceneIOController& sceneIO,
    EntityId entity
) {
    if (!ImGui::BeginPopupContextItem()) return;

    char ctxName[64];
    getEntityDisplayName(scene, entity, ctxName, sizeof(ctxName));
    ImGui::TextDisabled("%s", ctxName);
    ImGui::Separator();

    if (ImGui::MenuItem("Select")) state.selectEntity(entity);
    // On a row inside the multi-selection the ops act on the whole set.
    const bool onSelection = state.isSelected(entity) && state.selection.size() > 1;
    if (ImGui::MenuItem("Duplicate", keyLabel(state.prefs.keybinds.duplicate))) {
        if (onSelection) EditorActions::duplicateSelection(scene, resources, state);
        else             EditorActions::duplicateEntity(scene, resources, state, entity);
    }
    if (ImGui::MenuItem("Delete", keyLabel(state.prefs.keybinds.deleteEntity))) {
        if (onSelection) EditorActions::deleteSelection(scene, state);
        else             EditorActions::deleteEntity(scene, state, entity);
    }

    // Not during play: the pose is where physics put it, and would overwrite the authored one.
    const bool playing = sceneIO.isPlaying();
    if (ImGui::MenuItem("Save as Prefab", nullptr, false, !playing)) {
        EditorActions::saveAsPrefab(scene, resources, state, entity);
    }
    if (playing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Stop the play session to save a prefab");

    const Hierarchy* link = scene.tryGet<Hierarchy>(entity);
    if (link && link->parent) {
        if (ImGui::MenuItem("Unparent")) {
            EditorActions::reparentKeepingWorld(scene, state, entity, EntityId{}, "Unparent");
        }
    }

    if (scene.has<Transform>(entity)) {
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Transform")) {
            EditScope<Transform> t(scene, resources, state, entity, "Reset Transform");
            t->position = glm::vec3(0.0f);
            t->rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            t->scale    = glm::vec3(1.0f);
        }
    }

    if (const Light* held = scene.tryGet<Light>(entity)) {
        const bool lit = held->enabled;
        if (ImGui::MenuItem(lit ? "Disable Light" : "Enable Light")) {
            EditScope<Light> light(scene, resources, state, entity, "Toggle Light");
            light->enabled = !lit;
        }
    }

    if (const Mesh* held = scene.tryGet<Mesh>(entity)) {
        const bool shown = held->visible;
        if (ImGui::MenuItem(shown ? "Hide" : "Show")) {
            EditScope<Mesh> mesh(scene, resources, state, entity, "Toggle Mesh Visibility");
            mesh->visible = !shown;
        }
    }

    if (const Camera* cam = scene.tryGet<Camera>(entity)) {
        ImGui::Separator();
        // Active is not exclusive: stay offered while another camera also claims it.
        bool otherActive = false;
        scene.forEach<Camera>([&](EntityId other, const Camera& c) {
            if (other != entity && c.active) otherActive = true;
        });
        const bool isOnlyActive = cam->active && !otherActive;
        if (ImGui::MenuItem("Set as Main Camera", nullptr, false, !isOnlyActive)) {
            pushActiveCamera(scene, resources, state, entity);
        }
    }

    ImGui::EndPopup();
}

} // namespace Vkm::Engine
