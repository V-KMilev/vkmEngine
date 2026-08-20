#include "panels/hierarchy_panel.h"

#include <cfloat>
#include <cstring>
#include <utility>
#include <vector>

#include "framework/component_edit.h"
#include "framework/editor_common.h"
#include "ui/editor_style.h"
#include "framework/editor_commands.h"
#include "framework/editor_actions.h"
#include "framework/prefab_overrides.h"
#include "system/script/script_component.h"

namespace Vkm::Engine {

namespace {
// One click policy for every hierarchy row.
void rowClickSelect(EditorState& state, EntityId id) {
    if (ImGui::GetIO().KeyCtrl)       state.toggleSelection(id);
    else if (ImGui::GetIO().KeyShift) state.addToSelection(id);
    else                              state.selectEntity(id);
}
} // namespace

namespace {
/**
 * @brief True if @p maybeAncestor is @p node itself or anywhere up its parent
 * chain. Used to reject drag-reparenting that would create a cycle.
 */
bool isSelfOrAncestor(const Scene& scene, EntityId node, EntityId maybeAncestor) {
    EntityId cur = node;
    for (int guard = 0; cur && guard < 64; ++guard) {
        if (cur == maybeAncestor) return true;
        if (!scene.has<Hierarchy>(cur)) break;
        cur = scene.get<Hierarchy>(cur).parent;
    }
    return false;
}

// Entities the hierarchy lists: 3D nodes (Transform) and screen-space UI nodes
// (UICanvas / UIElement), so UI entities appear in the tree alongside the scene
// graph even though they carry no Transform.
bool isHierarchyNode(const Scene& scene, EntityId id) {
    return scene.has<Transform>(id) || scene.has<UICanvas>(id) || scene.has<UIElement>(id);
}
}

void HierarchyPanel::draw(EditorContext& ec) {
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
        // Rebuild root list only when entities change (not every frame).
        size_t currentCount = scene.entityCount();
        bool wasDirty = state.hierarchyDirty || currentCount != m_lastEntityCount;
        if (wasDirty) {
            m_cachedRoots.clear();
            m_cachedRoots.reserve(currentCount);
            scene.forEachEntity([&](EntityId id) {
                if (!isHierarchyNode(scene, id)) return;
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
                scene.forEachEntity([&](EntityId id) {
                    if (!isHierarchyNode(scene, id)) return;
                    char name[64];
                    getEntityDisplayName(scene, id, name, sizeof(name));
                    if (matchesFilter(name, m_filter))
                        m_cachedFiltered.push_back(id);
                });
            }
        }

        // The scene's World node: not an entity, just the handle for editing
        // scene-global settings. Pinned at the top; selecting it shows those
        // settings in the Inspector.
        if (!hasFilter) {
            static char worldNodeId = 0;   // stable address -> a unique ImGui id for this non-entity row
            // FramePadding matches the entity rows' height - without it this
            // first, shorter row let the icon glyph poke past the child's top
            // clip (visibly shaved against the search bar).
            ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                 | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
            if (state.worldSelected) f |= ImGuiTreeNodeFlags_Selected;
            entityTreeNode(static_cast<void*>(&worldNodeId), f, EditorIcon::SpaceWorld, "World");
            if (ImGui::IsItemClicked()) state.selectWorld();
            ImGui::Separator();
        }

        // Empty scene: a centered call to action instead of a bare list.
        if (displayList.empty() && !hasFilter) {
            ImGui::Spacing();
            ImGui::Spacing();
            const char* line = "The scene is empty.";
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x
                                  - ImGui::CalcTextSize(line).x) * 0.5f);
            ImGui::TextDisabled("%s", line);
            const float btnW = EditorStyle::px(170.0f);
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - btnW) * 0.5f);
            if (ImGui::Button("+  Create Entity", ImVec2(btnW, 0)))
                ImGui::OpenPopup("##CreateEmptyState");
            // Same-scope popup: the header's "##CreatePopup" is begun at panel
            // scope and would never match an OpenPopup from inside this child.
            if (ImGui::BeginPopup("##CreateEmptyState")) {
                EditorActions::drawCreateEntityMenu(scene, ec.frame.resources, state);
                ImGui::EndPopup();
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
                                         | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
                    if (state.isSelected(id)) f |= ImGuiTreeNodeFlags_Selected;
                    char name[64];
                    getEntityDisplayName(scene, id, name, sizeof(name));
                    entityTreeNode(reinterpret_cast<void*>(static_cast<uintptr_t>(id.index)),
                                   f, entityIconKind(scene, id), name);
                    if (ImGui::IsItemClicked()) rowClickSelect(state, id);
                    drawEntityContextMenu(scene, resources, state, id);
                } else {
                    drawEntityNode(scene, resources, state, id);
                }
            }
        }

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            && !ImGui::IsAnyItemHovered()) {
            state.deselect();
        }

        if (ImGui::BeginPopupContextWindow("##HierarchyCtx", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight)) {
            EditorActions::drawCreateEntityMenu(scene, ctx.resources, state);
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();

    ImGui::TextDisabled("%zu entities", scene.entityCount());
}

void HierarchyPanel::drawEntityNode(Scene& scene, ResourceManager& resources,
                                    EditorState& state, EntityId entity) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_SpanAvailWidth
                             | ImGuiTreeNodeFlags_FramePadding;

    bool hasChildren = scene.has<Hierarchy>(entity) && scene.get<Hierarchy>(entity).firstChild;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (state.isSelected(entity)) flags |= ImGuiTreeNodeFlags_Selected;

    char name[64];
    getEntityDisplayName(scene, entity, name, sizeof(name));

    // Inline rename: the tree-node label becomes an InputText. Commit on
    // Enter / focus loss, cancel on Escape; F2 / double-click starts a session.
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
            if (!scene.has<Name>(entity)) {
                scene.add(entity, makeName(m_renameBuf));
                // Adding a Name where none existed: undo removes it (reverts to
                // the default display name).
                state.commands.push(std::make_unique<AddComponentCommand<Name>>(
                    entity, makeName(m_renameBuf), "Rename"));
                // A Name the prefab does not define cannot be an override, so
                // inside an instance this one lives only until the next load.
                PrefabOverrides::warnComponentIsPrefabs(scene, state, entity, "Name",
                                                        "is not stored in the scene");
            } else {
                auto& n = scene.get<Name>(entity);
                const Name before = n;
                n = makeName(m_renameBuf);
                pushEdit<Name>(scene, resources, state, entity, before, n, "Rename");
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

    if (state.selectedEntity == entity && ImGui::IsItemHovered()
            && (ImGui::IsKeyPressed(ImGuiKey_F2)
             || ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) {
        m_renameTarget = entity;
        m_renameFocusNeeded = true;
        std::strncpy(m_renameBuf, name, sizeof(m_renameBuf) - 1);
        m_renameBuf[sizeof(m_renameBuf) - 1] = '\0';
    }

    // Hover tooltip: covers names truncated in a narrow panel, plus a
    // component digest.
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
        if (scene.has<Animator>(entity))  append("Animator");
        if (scene.has<Rigidbody>(entity))        append("Rigidbody");
        if (scene.has<Collider>(entity))         append("Collider");
        if (scene.has<CharacterController>(entity)) append("Character");
        if (scene.has<ScriptComponent>(entity))  append("Script");
        if (scene.has<ReflectionProbe>(entity))  append("Probe");
        if (scene.has<IrradianceVolume>(entity)) append("GI Volume");
        if (scene.has<Decal>(entity))            append("Decal");
        if (scene.has<ParticleEmitter>(entity))  append("Particles");
        if (scene.has<UICanvas>(entity))         append("Canvas");
        if (scene.has<UIElement>(entity))        append("UI Element");
        if (scene.has<UIImage>(entity))          append("Image");
        if (scene.has<UIText>(entity))           append("Text");
        if (scene.has<UIButton>(entity))         append("Button");
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
            // Dropping onto self or onto a descendant would create a cycle.
            if (scene.isAlive(dragged) && !isSelfOrAncestor(scene, entity, dragged)) {
                EditorActions::reparentKeepingWorld(scene, state, dragged, entity, "Reparent Entity");
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) rowClickSelect(state, entity);

    drawEntityContextMenu(scene, resources, state, entity);

    if (nodeOpen && hasChildren) {
        HierarchyOperations::forEachChild(scene, entity, [&](EntityId child) {
            drawEntityNode(scene, resources, state, child);
        });
        ImGui::TreePop();
    }
}

void HierarchyPanel::drawEntityContextMenu(Scene& scene, ResourceManager& resources,
                                           EditorState& state, EntityId entity) {
    if (!ImGui::BeginPopupContextItem()) return;

    char ctxName[64];
    getEntityDisplayName(scene, entity, ctxName, sizeof(ctxName));
    ImGui::TextDisabled("%s", ctxName);
    ImGui::Separator();

    if (ImGui::MenuItem("Select")) state.selectEntity(entity);
    // On a row inside the multi-selection the ops act on the whole set;
    // a right-click on an unselected row stays single-entity.
    const bool onSelection = state.isSelected(entity) && state.selection.size() > 1;
    if (ImGui::MenuItem("Duplicate", keyLabel(state.keybinds.duplicate))) {
        if (onSelection) EditorActions::duplicateSelection(scene, resources, state);
        else             EditorActions::duplicateEntity(scene, resources, state, entity);
    }
    if (ImGui::MenuItem("Delete", keyLabel(state.keybinds.deleteEntity))) {
        if (onSelection) EditorActions::deleteSelection(scene, state);
        else             EditorActions::deleteEntity(scene, state, entity);
    }

    // Saving an instance back over its own prefab is how a prefab is edited, so
    // this is offered whether or not the entity already is one.
    if (ImGui::MenuItem("Save as Prefab")) {
        EditorActions::saveAsPrefab(scene, resources, state, entity);
    }

    if (scene.has<Hierarchy>(entity) && scene.get<Hierarchy>(entity).parent) {
        if (ImGui::MenuItem("Unparent")) {
            EditorActions::reparentKeepingWorld(scene, state, entity, EntityId{}, "Unparent");
        }
    }

    if (scene.has<Transform>(entity)) {
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Transform")) {
            auto& t = scene.get<Transform>(entity);
            const Transform before = t;
            t.position = glm::vec3(0.0f);
            t.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            t.scale    = glm::vec3(1.0f);
            pushEdit<Transform>(scene, resources, state, entity, before, t, "Reset Transform");
            EditorActions::commitHierarchyMutation(state);
        }
    }

    if (scene.has<Light>(entity)) {
        auto& light = scene.get<Light>(entity);
        if (ImGui::MenuItem(light.enabled ? "Disable Light" : "Enable Light")) {
            const Light before = light;
            light.enabled = !light.enabled;
            pushEdit<Light>(scene, resources, state, entity, before, light, "Toggle Light");
        }
    }

    if (scene.has<Mesh>(entity)) {
        auto& mesh = scene.get<Mesh>(entity);
        if (ImGui::MenuItem(mesh.visible ? "Hide" : "Show")) {
            const Mesh before = mesh;
            mesh.visible = !mesh.visible;
            pushEdit<Mesh>(scene, resources, state, entity, before, mesh, "Toggle Mesh Visibility");
        }
    }

    if (scene.has<Camera>(entity)) {
        ImGui::Separator();
        const auto& cam = scene.get<Camera>(entity);
        const bool isActive = cam.active;
        if (ImGui::MenuItem("Set as Main Camera", nullptr, false, !isActive)) {
            EditorActions::setActiveCamera(scene, state, entity);
        }
    }

    ImGui::EndPopup();
}

} // namespace Vkm::Engine
