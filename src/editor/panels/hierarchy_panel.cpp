#include "panels/hierarchy_panel.h"

#include <algorithm>
#include <cfloat>
#include <cstddef>
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
#include "io/scene/prefab.h"
#include "resource/resource_manager.h"
#include "ui/editor_icons.h"
#include "ui/editor_widgets.h"
#include "ui/entity_labels.h"
#include "ui/editor_style.h"
#include "editor_actions.h"
#include "panels/asset_browser_panel.h"
#include "panels/inspector_panel.h"
#include "ui/asset_drag.h"

namespace Vkm::Engine {

void HierarchyPanel::draw(EditorContext& ec, SceneIOController& sceneIO) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;
    auto& scene     = ctx.scene;
    auto& resources = ctx.resources;

    float btnW = ImGui::GetFrameHeight();
    searchField("##Filter", m_filter, sizeof(m_filter), -(btnW + ImGui::GetStyle().ItemSpacing.x));
    ImGui::SameLine();
    if (iconButton("create", EditorIcon::Plus, false, true, "Create Entity", btnW)) {
        ImGui::OpenPopup("##CreatePopup");
    }
    if (ImGui::BeginPopup("##CreatePopup")) {
        EditorActions::drawCreateEntityMenu(scene, ctx.resources, state);
        ImGui::EndPopup();
    }
    ImGui::Spacing();

    const bool hasFilter = m_filter[0] != '\0';
    revealSelection(scene, state);

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

        if (rowCount == 0 && hasFilter) {
            ImGui::Spacing();
            ImGui::TextDisabled("No entity matches the search.");
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
        // The row asked for is drawn even off screen, so it can scroll itself into view.
        if (m_scrollTo) {
            const std::vector<EntityId>& rows = rowEntities();
            const auto at = std::find(rows.begin(), rows.end(), m_scrollTo);
            if (at != rows.end()) clipper.IncludeItemByIndex(static_cast<int>(at - rows.begin()));
            else                  m_scrollTo = {};
        }
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                if (hasFilter) {
                    // A search's rows act as the tree's: drag, rename, the digest and the tint.
                    const EntityId id = m_filtered[static_cast<size_t>(i)];
                    drawEntityNode(scene, resources, state, sceneIO, id, 0, true);
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

        // The space under the rows takes a dragged entity out to the root.
        const ImVec2 rest = ImGui::GetContentRegionAvail();
        ImGui::Dummy(ImVec2(std::max(rest.x, 1.0f), std::max(rest.y, ImGui::GetFrameHeight())));
        // An asset dragged there from the Assets panel is placed where the view looks.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("VKM_ENTITY")) {
                const EntityId dragged = *static_cast<const EntityId*>(pl->Data);
                EditorActions::reparentKeepingWorld(scene, state, dragged, EntityId{}, "Unparent");
            }
            AssetBrowserPanel::acceptAssetCreate(scene, resources, state);
            ImGui::EndDragDropTarget();
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

void HierarchyPanel::revealSelection(const Scene& scene, const EditorState& state) {
    if (state.selectedEntity == m_revealed) return;
    m_revealed = state.selectedEntity;
    if (!scene.isAlive(m_revealed)) return;

    HierarchyOperations::forSelfAndAncestors(scene, m_revealed, [&](EntityId at) {
        if (at != m_revealed) m_open.insert(at.slot());
        return true;
    });
    m_scrollTo = m_revealed;
}

const std::vector<EntityId>& HierarchyPanel::rowEntities() {
    if (m_filter[0] != '\0') return m_filtered;
    m_rowEntities.clear();
    for (const Row& row : m_visible) m_rowEntities.push_back(row.entity);
    return m_rowEntities;
}

void HierarchyPanel::clickRow(EditorState& state, EntityId entity) {
    const ImGuiIO& io = ImGui::GetIO();
    const EntityId anchor = state.selectedEntity;
    if (io.KeyShift && !io.KeyCtrl && anchor && anchor != entity) {
        const std::vector<EntityId>& rows = rowEntities();
        const auto from = std::find(rows.begin(), rows.end(), anchor);
        const auto to   = std::find(rows.begin(), rows.end(), entity);
        if (from != rows.end() && to != rows.end()) {
            const ptrdiff_t step = to > from ? 1 : -1;
            for (auto at = from; at != to;) {
                at += step;
                state.addToSelection(*at);
            }
            m_revealed = state.selectedEntity;  // clicked in view: nothing to scroll to
            return;
        }
    }
    state.clickSelect(entity);
    m_revealed = state.selectedEntity;
}

void HierarchyPanel::dropOnRow(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId entity
) {
    if (!ImGui::BeginDragDropTarget()) return;

    // An asset from the Assets panel is set on the row's entity, not placed beside it.
    if (peekAssetDrag()) {
        AssetBrowserPanel::acceptAssetOn(scene, resources, state, entity);
        ImGui::EndDragDropTarget();
        return;
    }

    // A quarter of the row at each edge places beside it; the middle reparents under it.
    // Roots stand in slot order, so beside a root is only under it.
    const Hierarchy* node    = scene.tryGet<Hierarchy>(entity);
    const EntityId   parent  = node ? node->parent : EntityId{};
    const ImVec2     min     = ImGui::GetItemRectMin();
    const ImVec2     max     = ImGui::GetItemRectMax();
    const float      y       = ImGui::GetMousePos().y;
    const float      edge    = (max.y - min.y) * 0.25f;
    const bool       above   = parent && y < min.y + edge;
    const bool       below   = parent && y > max.y - edge;
    const ImGuiDragDropFlags flags = above || below ? ImGuiDragDropFlags_AcceptNoDrawDefaultRect : 0;

    const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("VKM_ENTITY", flags);
    if (above || below) {
        const float lineY = above ? min.y : max.y;
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(min.x, lineY),
            ImVec2(max.x, lineY),
            EditorStyle::HIGHLIGHT_U32,
            EditorStyle::px(2.0f)
        );
    }
    if (pl) {
        const EntityId dragged = *static_cast<const EntityId*>(pl->Data);
        if (above) {
            EditorActions::reparentKeepingWorld(scene, state, dragged, parent, "Move Entity", entity);
        } else if (below) {
            // After it: in front of whatever follows it, skipping the dragged entity itself.
            EntityId next = node ? node->nextSibling : EntityId{};
            if (next == dragged) {
                const Hierarchy* skipped = scene.tryGet<Hierarchy>(next);
                next = skipped ? skipped->nextSibling : EntityId{};
            }
            EditorActions::reparentKeepingWorld(scene, state, dragged, parent, "Move Entity", next);
        } else {
            EditorActions::reparentKeepingWorld(scene, state, dragged, entity, "Reparent Entity");
        }
    }
    ImGui::EndDragDropTarget();
}

void HierarchyPanel::drawEntityNode(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    SceneIOController& sceneIO,
    EntityId entity,
    int depth,
    bool found
) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_SpanAvailWidth
        | ImGuiTreeNodeFlags_FramePadding
        | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    const Hierarchy* node = scene.tryGet<Hierarchy>(entity);
    const bool hasChildren = !found && node && node->firstChild;
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
        drawRenameField(scene, resources, state, entity);
        return;  // the rename field replaces this row; its children are their own rows
    }

    // Forced from m_open, read back after: the arrow's click applies inside this call.
    if (hasChildren) ImGui::SetNextItemOpen(m_open.count(entity.slot()) != 0);

    // What a prefab instance builds reads in its color: the editor answers it as the prefab's.
    const bool fromPrefab = static_cast<bool>(Prefab::instanceRootOf(scene, entity));
    if (fromPrefab) {
        const ImVec4 plain = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        const ImVec4 tint  = EditorStyle::Accent::PREFAB;
        const float  share = 0.6f;
        const ImVec4 mixed(
            plain.x + (tint.x - plain.x) * share,
            plain.y + (tint.y - plain.y) * share,
            plain.z + (tint.z - plain.z) * share,
            plain.w
        );
        ImGui::PushStyleColor(ImGuiCol_Text, mixed);
    }
    const bool nodeOpen = entityTreeNode(
        reinterpret_cast<void*>(static_cast<uintptr_t>(entity.slot())),
        flags,
        entityIconKind(scene, entity),
        name
    );
    if (fromPrefab) ImGui::PopStyleColor();

    if (hasChildren) {
        if (nodeOpen) m_open.insert(entity.slot());
        else          m_open.erase(entity.slot());
    }

    if (state.selectedEntity == entity && ImGui::IsItemHovered()
        && (ImGui::IsKeyPressed(RENAME_KEY) || ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) {
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
#define VKM_HIERARCHY_DIGEST_ROW(Component, method, title, accent, section, hint) \
        if (scene.has<Component>(entity)) {                                       \
            ImGui::TextUnformatted(title);                                        \
            any = true;                                                           \
        }
        VKM_INSPECTOR_CARDS(VKM_HIERARCHY_DIGEST_ROW)
#undef VKM_HIERARCHY_DIGEST_ROW
        if (!any) ImGui::TextUnformatted("(no components)");
        ImGui::EndTooltip();
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload("VKM_ENTITY", &entity, sizeof(EntityId));
        ImGui::Text("Move %s", name);
        ImGui::EndDragDropSource();
    }
    dropOnRow(scene, resources, state, entity);

    if (entity == m_scrollTo) {
        if (!ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.5f);
        m_scrollTo = {};
    }

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) clickRow(state, entity);

    drawEntityContextMenu(scene, resources, state, sceneIO, entity);
}

void HierarchyPanel::drawRenameField(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId entity
) {
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
    } else if (cancelled || lostFocus || committed) {
        m_renameTarget = {};
    }
    ImGui::PopID();
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
    if (ImGui::MenuItem("Rename", ImGui::GetKeyName(RENAME_KEY))) {
        m_renameTarget      = entity;
        m_renameFocusNeeded = true;
        std::strncpy(m_renameBuf, ctxName, sizeof(m_renameBuf) - 1);
        m_renameBuf[sizeof(m_renameBuf) - 1] = '\0';
    }
    // A prefab instance's interior is rebuilt from its file, so a child made there is lost.
    if (Prefab::instanceRootOf(scene, entity)) {
        ImGui::BeginDisabled();
        ImGui::MenuItem("Create Child");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("A prefab instance is built from its prefab; open the prefab to add to it");
        }
    } else {
        EditorActions::drawCreateEntityMenu(scene, resources, state, entity);
    }
    // On a row inside the multi-selection the ops act on the whole set.
    const bool onSelection = state.isSelected(entity) && state.selection.size() > 1;
    if (ImGui::MenuItem("Copy", keyLabel(state.prefs.keybinds.copyEntity))) {
        // The row alone unless it is part of the selection, as the other ops here.
        if (!onSelection) state.selectEntity(entity);
        EditorActions::copySelection(scene, resources, state);
    }
    const bool copied = state.entityClipboard != nullptr;
    if (ImGui::MenuItem("Paste", keyLabel(state.prefs.keybinds.pasteEntity), false, copied)) {
        EditorActions::paste(scene, resources, state);
    }
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
