#include "panels/hierarchy_panel.h"

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

#include <imgui.h>

#include "session/play_session.h"
#include "undo/component_edit.h"
#include "core/frame_context.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/component/scene/name.h"
#include "ecs/scene.h"
#include "ecs/hierarchy_operations.h"
#include "editor_context.h"
#include "editor_state.h"
#include "input/editor_keybinds.h"
#include "io/scene/prefab.h"
#include "ecs/prefab_instances.h"
#include "resource/resource_manager.h"
#include "ui/style/editor_icons.h"
#include "ui/editor_widgets.h"
#include "ui/entity_labels.h"
#include "ui/style/editor_style.h"
#include "actions/edit_actions.h"
#include "actions/create_menu.h"
#include "actions/entity_clipboard.h"
#include "actions/prefab_actions.h"
#include "actions/component_actions.h"
#include "panels/inspector/card_info.h"
#include "ui/asset/asset_drag.h"
#include "actions/asset_drop.h"

namespace Vkm::Engine {

void HierarchyPanel::draw(EditorContext& ec) {
    const PlaySession& play = ec.play;
    FrameContext& ctx = ec.frame;
    EditorState& state = ec.state;
    auto& scene     = ctx.scene;
    auto& resources = ctx.resources;

    float btnW = ImGui::GetFrameHeight();
    searchField("##Filter", m_filter, sizeof(m_filter), -(btnW + ImGui::GetStyle().ItemSpacing.x));
    ImGui::SameLine();
    if (iconButton("create", EditorIcon::Plus, false, true, "Create Entity", btnW)) {
        ImGui::OpenPopup("##CreatePopup");
    }
    if (ImGui::BeginPopup("##CreatePopup")) {
        CreateMenu::items(scene, ctx.resources, state);
        ImGui::EndPopup();
    }
    ImGui::Spacing();

    const bool hasFilter = m_filter[0] != '\0';
    revealSelection(scene, state);

    // ImGui's navigation is off: stepWithKeys steps the rows, so no nav ring parts from the selection.
    const ImVec2 treeSize(0.0f, -ImGui::GetFrameHeightWithSpacing());
    if (ImGui::BeginChild("##Tree", treeSize, ImGuiChildFlags_None, ImGuiWindowFlags_NoNavInputs)) {
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

        // Touching rows, 22 px at the 14 px font, read as one list and fit more.
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, EditorStyle::px(4.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0f));

        // The World node: not an entity, the handle for scene-global settings.
        if (!hasFilter) {
            if (listRow("##world", EditorIcon::SpaceWorld, "World", state.selection.world()).clicked) {
                state.selection.selectWorld();
            }
            ImGui::Separator();
        }

        if (rowCount == 0 && hasFilter) {
            ImGui::Spacing();
            ImGui::TextDisabled("No entity matches the search.");
        }
        if (rowCount == 0 && !hasFilter) {
            ImGui::Spacing();
            ImGui::Spacing();
            const char* line  = "The scene is empty.";
            const float lineW = ImGui::CalcTextSize(line).x;
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - lineW) * 0.5f);
            ImGui::TextDisabled("%s", line);
            const float createW = EditorStyle::px(170.0f);
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - createW) * 0.5f);
            if (ImGui::Button("+  Create Entity", ImVec2(createW, 0)))
                ImGui::OpenPopup("##CreateEmptyState");
            // Its own popup: the header's "##CreatePopup" is at panel scope and would not match here.
            if (ImGui::BeginPopup("##CreateEmptyState")) {
                CreateMenu::items(scene, ec.frame.resources, state);
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
                    drawEntityNode(scene, resources, state, play, id, 0, true);
                } else {
                    const Row& row = m_visible[static_cast<size_t>(i)];
                    drawEntityNode(scene, resources, state, play, row.entity, row.depth);
                }
            }
        }

        ImGui::PopStyleVar(2);
        stepWithKeys(scene, state);

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            && !ImGui::IsAnyItemHovered()) {
            state.selection.clear();
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
            acceptAssetCreate(scene, resources, state);
            ImGui::EndDragDropTarget();
        }

        const bool menuOpen = ImGui::BeginPopupContextWindow(
            "##HierarchyCtx",
            ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight
        );
        if (menuOpen) {
            CreateMenu::items(scene, ctx.resources, state);
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
    if (state.selection.active() == m_revealed) return;
    m_revealed = state.selection.active();
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

void HierarchyPanel::stepWithKeys(const Scene& scene, EditorState& state) {
    if (!ImGui::IsWindowFocused() || ImGui::IsAnyItemActive()) return;
    const bool up    = ImGui::IsKeyPressed(ImGuiKey_UpArrow);
    const bool down  = ImGui::IsKeyPressed(ImGuiKey_DownArrow);
    const bool left  = ImGui::IsKeyPressed(ImGuiKey_LeftArrow);
    const bool right = ImGui::IsKeyPressed(ImGuiKey_RightArrow);
    if (!up && !down && !left && !right) return;

    const std::vector<EntityId>& rows = rowEntities();
    if (rows.empty()) return;
    const EntityId active = state.selection.active();
    const auto at = std::find(rows.begin(), rows.end(), active);
    if (at == rows.end()) {
        state.selectEntity(up ? rows.back() : rows.front());
        return;
    }
    if (up || down) {
        const ptrdiff_t next = (at - rows.begin()) + (up ? -1 : 1);
        if (next >= 0 && next < static_cast<ptrdiff_t>(rows.size())) state.selectEntity(rows[next]);
        return;
    }

    // A search's rows are flat: nothing opens there.
    if (m_filter[0] != '\0') return;
    const Hierarchy* node = scene.tryGet<Hierarchy>(active);
    const bool open = m_open.count(active.slot()) != 0;
    if (left && node && node->firstChild && open) {
        m_open.erase(active.slot());
    } else if (left && node && node->parent) {
        state.selectEntity(node->parent);
    } else if (right && node && node->firstChild && !open) {
        m_open.insert(active.slot());
    } else if (right && node && node->firstChild) {
        state.selectEntity(node->firstChild);
    }
}

void HierarchyPanel::clickRow(EditorState& state, EntityId entity) {
    const ImGuiIO& io     = ImGui::GetIO();
    const EntityId anchor = state.selection.active();
    if (io.KeyShift && !io.KeyCtrl && anchor && anchor != entity) {
        const std::vector<EntityId>& rows = rowEntities();
        const auto from = std::find(rows.begin(), rows.end(), anchor);
        const auto to   = std::find(rows.begin(), rows.end(), entity);
        if (from != rows.end() && to != rows.end()) {
            const ptrdiff_t step = to > from ? 1 : -1;
            for (auto at = from; at != to;) {
                at += step;
                state.selection.add(*at);
            }
            m_revealed = state.selection.active();  // clicked in view: nothing to scroll to
            return;
        }
    }
    state.selection.click(entity, {io.KeyCtrl, io.KeyShift});
    m_revealed = state.selection.active();
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
        acceptAssetOn(scene, resources, state, entity);
        ImGui::EndDragDropTarget();
        return;
    }

    // A quarter of the row at each edge places beside it; the middle reparents under it.
    // Roots stand in slot order, so beside a root is only under it.
    const Hierarchy* node = scene.tryGet<Hierarchy>(entity);
    const EntityId parent = node ? node->parent : EntityId{};
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float y    = ImGui::GetMousePos().y;
    const float edge = (max.y - min.y) * 0.25f;
    const bool above = parent && y < min.y + edge;
    const bool below = parent && y > max.y - edge;
    const ImGuiDragDropFlags flags = above || below ? ImGuiDragDropFlags_AcceptNoDrawDefaultRect : 0;

    const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("VKM_ENTITY", flags);
    if (above || below) {
        const float lineY = above ? min.y : max.y;
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(min.x, lineY),
            ImVec2(max.x, lineY),
            ImGui::GetColorU32(EditorStyle::ACCENT),
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
    const PlaySession& play,
    EntityId entity,
    int depth,
    bool found
) {
    const Hierarchy* node  = scene.tryGet<Hierarchy>(entity);
    const bool hasChildren = !found && node && node->firstChild;

    char name[64];
    getEntityDisplayName(scene, entity, name, sizeof(name));

    if (m_renameTarget == entity) {
        // The rename field replaces this row, at its indent; its children are their own rows.
        const float indent = static_cast<float>(depth) * ImGui::GetStyle().IndentSpacing;
        if (indent > 0.0f) ImGui::Indent(indent);
        drawRenameField(scene, resources, state, entity);
        if (indent > 0.0f) ImGui::Unindent(indent);
        return;
    }

    // What a prefab instance builds is linked to it: the editor answers it as the prefab's.
    const bool fromPrefab = static_cast<bool>(Prefab::instanceRootOf(scene, entity));
    const bool open       = hasChildren && m_open.count(entity.slot()) != 0;
    char rowId[16];
    std::snprintf(rowId, sizeof(rowId), "%u", entity.slot());
    const ListRowResult row = listRow(
        rowId,
        entityIconKind(scene, entity),
        name,
        state.selection.contains(entity),
        depth,
        hasChildren,
        open,
        fromPrefab
    );
    if (row.toggled) {
        if (open) m_open.erase(entity.slot());
        else      m_open.insert(entity.slot());
    }

    if (state.selection.active() == entity && ImGui::IsItemHovered()
        && (ImGui::IsKeyPressed(RENAME_KEY) || ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) {
        m_renameTarget      = entity;
        m_renameFocusNeeded = true;
        std::strncpy(m_renameBuf, name, sizeof(m_renameBuf) - 1);
        m_renameBuf[sizeof(m_renameBuf) - 1] = '\0';
    }

    // Tooltip: the full name, for a narrow panel, plus a component digest.
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !row.toggled) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(name);
        ImGui::TextDisabled("#%u", entity.slot());
        ImGui::Separator();
        // The Inspector's card list, in the Inspector's order.
        bool any = false;
#define VKM_HIERARCHY_DIGEST_ROW(Type, Key)                               \
        if (!std::is_same_v<Type, Name> && scene.has<Type>(entity)) {     \
            ImGui::TextUnformatted(cardInfo(Tag<Type>{}).title);          \
            any = true;                                                   \
        }
        VKM_SCENE_COMPONENTS(VKM_HIERARCHY_DIGEST_ROW, VKM_HIERARCHY_DIGEST_ROW, VKM_HIERARCHY_DIGEST_ROW)
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

    if (row.clicked) clickRow(state, entity);

    drawEntityContextMenu(scene, resources, state, play, entity);
    drawViewToggles(state, entity);
}

void HierarchyPanel::drawViewToggles(EditorState& state, EntityId entity) {
    // Shown while the row is under the pointer, and kept while set, so a hidden row says so.
    const bool hidden = isIn(state.hiddenInView, entity);
    const bool locked = isIn(state.lockedInView, entity);
    // Still hovered while one of its own buttons is held, or the button would vanish under the click.
    const ImGuiHoveredFlags under = ImGuiHoveredFlags_AllowWhenOverlappedByItem
        | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem;
    const bool hover = ImGui::IsItemHovered(under);
    if (!hover && !hidden && !locked) return;

    const ImVec2 rowMin = ImGui::GetItemRectMin();
    const ImVec2 rowMax = ImGui::GetItemRectMax();
    const float  side   = rowMax.y - rowMin.y;
    const auto toggle = [&](const char* id, float x, EditorIcon icon, bool on, const char* tip) {
        ImGui::SetCursorScreenPos(ImVec2(x, rowMin.y));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        if (on) ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::ACCENT);
        const bool pressed = iconButton(id, icon, false, true, tip, side);
        ImGui::PopStyleColor(on ? 2 : 1);
        return pressed;
    };
    const KeyLabel hideKey = keyLabel(state.prefs.keybinds.hideSelection);
    char hideTip[96];
    std::snprintf(hideTip, sizeof(hideTip), "Hide in the editor's view (%s)", hideKey.buf);

    ImGui::PushID(static_cast<int>(entity.slot()));
    const float lockX   = rowMax.x - side - EditorStyle::px(4.0f);
    const float eyeX    = lockX - side;
    const char* eyeTip  = hidden ? "Show in the editor's view" : hideTip;
    const char* lockTip = locked ? "Let a click in the view pick it" : "Keep a click in the view off it";
    if (toggle("eye", eyeX, hidden ? EditorIcon::EyeOff : EditorIcon::Eye, hidden, eyeTip)) {
        toggleIn(state.hiddenInView, entity);
    }
    if (toggle("lock", lockX, locked ? EditorIcon::Lock : EditorIcon::LockOpen, locked, lockTip)) {
        toggleIn(state.lockedInView, entity);
    }
    ImGui::PopID();
    // Back to the row's end, so the next row starts where it would have.
    ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMax.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
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
    const bool canceled  = ImGui::IsKeyPressed(ImGuiKey_Escape);

    if (committed && m_renameBuf[0] != '\0') {
        if (!scene.tryGet<Name>(entity)) {
            // Undo removes the added Name, restoring the default display name.
            EditorActions::addComponent(scene, state, entity, makeName(m_renameBuf), "Name", "Rename");
        } else {
            EditScope<Name> n(scene, resources, state, entity, "Rename");
            *n = makeName(m_renameBuf);
        }
        m_renameTarget = {};
    } else if (canceled || lostFocus || committed) {
        m_renameTarget = {};
    }
    ImGui::PopID();
}

void HierarchyPanel::drawEntityContextMenu(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    const PlaySession& play,
    EntityId entity
) {
    if (!ImGui::BeginPopupContextItem()) return;

    char ctxName[64];
    getEntityDisplayName(scene, entity, ctxName, sizeof(ctxName));
    ImGui::TextDisabled("%s", ctxName);
    ImGui::Separator();

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
            ImGui::SetTooltip("A prefab instance is built from its prefab; Unpack it to add to it");
        }
    } else {
        CreateMenu::draw(scene, resources, state, entity);
    }
    // On a row inside the multi-selection the ops act on the whole set.
    const bool onSelection = state.selection.contains(entity) && state.selection.entities().size() > 1;
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
    const bool playing = play.isPlaying();
    if (ImGui::MenuItem("Save as Prefab", nullptr, false, !playing)) {
        EditorActions::saveAsPrefab(scene, resources, state, entity);
    }
    if (playing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Stop the play session to save a prefab");

    if (ImGui::MenuItem("Group", keyLabel(state.prefs.keybinds.groupSelection))) {
        if (!onSelection) state.selectEntity(entity);
        EditorActions::groupSelection(scene, state);
    }
    const Hierarchy* link = scene.tryGet<Hierarchy>(entity);
    if (link && link->parent) {
        if (ImGui::MenuItem("Unparent")) {
            EditorActions::reparentKeepingWorld(scene, state, entity, EntityId{}, "Unparent");
        }
    }

    ImGui::EndPopup();
}

} // namespace Vkm::Engine
