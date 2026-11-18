// The viewport as the author clicks and drags in it, with the world running as in the editor: a
// click picks the nearest triangle or a marker, a scale drag takes the collider along, and a terrain
// stroke held over frames is one step.

#include <algorithm>
#include <vector>

#include "actions/create_entity.h"
#include "actions/edit_actions.h"
#include "core/frame/ui_draw_data.h"
#include "core/frame/visibility.h"
#include "ecs/component/landscape/terrain.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/hierarchy_operations.h"
#include "overlays/gizmo/click_target.h"
#include "overlays/gizmo/gizmo_overlay.h"
#include "overlays/gizmo/gizmo_snap.h"
#include "overlays/gizmo/ui_rect_drag.h"
#include "overlays/terrain_tool_overlay.h"
#include "undo/component_edit.h"

#include "editor/desk.h"
#include "harness/check.h"

using namespace Vkm::Test;
using namespace Vkm::Engine::EditorActions;

namespace {

/// Where @p world shows on the display, by the frame's camera.
glm::vec2 onScreen(const FrameContext& ctx, const glm::vec3& world) {
    const glm::vec4 clip = ctx.visibility->camera.viewProjection * glm::vec4(world, 1.0f);
    const glm::vec2 ndc  = glm::vec2(clip) / clip.w;
    return {(ndc.x * 0.5f + 0.5f) * Pointer::WIDTH, (0.5f - ndc.y * 0.5f) * Pointer::HEIGHT};
}

EntityId placed(Desk& desk, EntityKind kind, const glm::vec3& at, const glm::vec3& scale = glm::vec3(1.0f)) {
    const EntityId id = createEntity(desk.scene(), desk.resources(), desk.state, kind);
    {
        EditScope<Transform> pose(desk.scene(), desk.resources(), desk.state, id, "Edit Transform");
        pose->position = at;
        pose->scale    = scale;
    }
    desk.state.commands.endGesture();
    return id;
}

} // namespace

TEST("a click picks the nearest triangle under the pointer, a meshless entity by its marker, and never a locked one") {
    Game game;
    EditorHost& editor = EditorHost::on(game);
    openProject();
    Pointer pointer;
    GizmoOverlay overlay;

    EntityId front, back, wall, lamp;
    // Each click: the pointer at a world point, pressed for a frame, let go the next.
    struct Click { int frame; glm::vec3 at; EntityId picked; };
    std::vector<Click> clicks;
    game.run(14, [&](int frame) {
        Desk& desk = editor.desk();
        Pointer::aimAt(desk.ec);
        if (frame == 1) {
            desk.ec.cameraController.setViewpoint(EditorViewpoint{{0.0f, 1.0f, 12.0f}});
            front = placed(desk, EntityKind::Cube, {0.0f, 1.0f, 0.0f});
            back  = placed(desk, EntityKind::Cube, {0.0f, 1.0f, -6.0f});
            placed(desk, EntityKind::Cone, {-3.0f, 1.0f, 0.0f});
            wall  = placed(desk, EntityKind::Cube, {0.0f, 1.0f, -10.0f}, {40.0f, 20.0f, 1.0f});
            lamp  = placed(desk, EntityKind::PointLight, {3.0f, 1.0f, 2.0f});
            clicks = {{3, {0.0f, 1.0f, 0.5f}, {}},
                      // Beside the cone's tip: inside its box, on no triangle of it.
                      {5, {-3.0f + 0.45f, 1.45f, 0.0f}, {}},
                      {7, {3.0f, 1.0f, 2.0f}, {}},
                      {9, {0.0f, 1.0f, 0.5f}, {}}};
        }
        if (frame == 9) desk.state.lockedInView.push_back(front);
        for (Click& click : clicks) {
            if (frame != click.frame && frame != click.frame + 1) continue;
            pointer.moveTo(onScreen(desk.ec.frame, click.at));
            pointer.button(frame == click.frame);
            pointer.frame([&] {
                overlay.markers().markLights(desk.ec);
                overlay.handleViewportPick(desk.ec);
            });
            if (frame == click.frame) click.picked = desk.state.selection.active();
        }
    }, 0);

    REQUIRE(clicks.size() == 4);
    CHECK(clicks[0].picked == front);
    CHECK(clicks[1].picked == wall);
    CHECK(clicks[2].picked == lamp);
    CHECK(clicks[3].picked == back);
}

TEST("a click on the game's UI selects the element under it, unless the Hierarchy's lock holds it") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    const EntityId button = desk.scene().createEntity();
    Visibility seen;
    seen.hasCamera = true;
    UIDrawData drawn;
    drawn.pointerTarget      = button;
    desk.ec.frame.visibility = &seen;
    desk.ec.frame.ui         = &drawn;
    Pointer pointer;
    GizmoOverlay overlay;
    Pointer::aimAt(desk.ec);
    desk.ec.input.pointer = PointerOwner::GameUI;
    const auto click = [&] {
        pointer.button(true);
        pointer.frame([&] { overlay.handleViewportPick(desk.ec); });
        pointer.button(false);
        pointer.frame([] {});
    };

    desk.state.lockedInView = {button};
    click();
    CHECK(desk.state.selection.active() != button);
    desk.state.lockedInView.clear();
    click();
    CHECK(desk.state.selection.active() == button);
}

TEST("an Alt copy never dragged off its original is taken back whole, the unsaved mark too") {
    Game game;
    EditorHost& editor = EditorHost::on(game);
    openProject();
    Pointer pointer;
    GizmoOverlay overlay;

    EntityId box;
    size_t made   = 0;
    size_t copied = 0;
    game.run(8, [&](int frame) {
        Desk& desk = editor.desk();
        Pointer::aimAt(desk.ec);
        if (frame == 1) {
            desk.ec.cameraController.setViewpoint(EditorViewpoint{{0.0f, 1.0f, 8.0f}});
            box = placed(desk, EntityKind::Cube, {0.0f, 1.0f, 0.0f});
            desk.state.selection.select(box);
            desk.state.workspace.tool = EditorTool::Scale;
            desk.state.sceneDirty     = false;
            made                      = desk.scene().entityCount();
        }
        // The view frames the box by frame 3.
        if (frame < 3) return;
        // On the X handle's shaft with Alt held, pressed at 4 and let go at 6 where it was.
        pointer.moveTo(onScreen(desk.ec.frame, {0.0f, 1.0f, 0.0f}) + glm::vec2(40.0f, 0.0f));
        pointer.key(ImGuiMod_Alt, frame >= 4 && frame <= 6);
        pointer.button(frame >= 4 && frame <= 5);
        pointer.frame([&] { overlay.drawTransformGizmo(desk.ec); });
        copied = std::max(copied, desk.scene().entityCount());
    }, 0);

    Desk& desk = editor.desk();
    REQUIRE(copied == made + 1);
    CHECK(desk.scene().entityCount() == made);
    CHECK(!desk.state.commands.canRedo());
    CHECK(!desk.state.sceneDirty);
}

TEST("a box the gizmo scales takes its collider with it as it goes, and one undo puts both back") {
    Game game;
    EditorHost& editor = EditorHost::on(game);
    openProject();
    Pointer pointer;
    GizmoOverlay overlay;

    EntityId box;
    glm::vec2 origin(0.0f);
    std::vector<glm::vec2> drawn;  // Each frame of the drag: the box's X scale, and its collider's half width.
    game.run(14, [&](int frame) {
        Desk& desk = editor.desk();
        Pointer::aimAt(desk.ec);
        if (frame == 1) {
            desk.ec.cameraController.setViewpoint(EditorViewpoint{{0.0f, 1.0f, 8.0f}});
            box = placed(desk, EntityKind::Cube, {0.0f, 1.0f, 0.0f});
            desk.scene().add(box, Collider{});
            desk.state.selection.select(box);
            desk.state.workspace.tool = EditorTool::Scale;
        }
        // The view frames the box by frame 3.
        if (frame < 3) return;
        origin = onScreen(desk.ec.frame, {0.0f, 1.0f, 0.0f});
        // On the X handle's shaft from frame 3, pressed at 4, drawn out to twice as far by 9, let go at 10.
        const float along = 40.0f + 40.0f * static_cast<float>(std::clamp(frame - 4, 0, 5)) / 5.0f;
        pointer.moveTo(origin + glm::vec2(along, 0.0f));
        pointer.button(frame >= 4 && frame <= 9);
        pointer.frame([&] { overlay.drawTransformGizmo(desk.ec); });
        if (frame >= 5 && frame <= 9) {
            const float wide = desk.scene().get<Transform>(box).scale.x;
            drawn.emplace_back(wide, desk.scene().get<Collider>(box).parts[0].halfExtents.x);
        }
    }, 0);

    Desk& desk = editor.desk();
    REQUIRE(drawn.size() == 5);
    CHECK(drawn.back().x > 1.5f);
    // The collider kept up with the box on every frame of the drag, not only at its end.
    for (const glm::vec2& seen : drawn) CHECK_NEAR(seen.y, 0.5f * seen.x, 1e-5f);
    const Collider& scaled = desk.scene().get<Collider>(box);
    CHECK_NEAR(scaled.parts[0].halfExtents, glm::vec3(0.5f * drawn.back().x, 0.5f, 0.5f), 1e-5f);

    undo(desk.scene(), desk.state);
    CHECK(desk.scene().get<Transform>(box).scale == glm::vec3(1.0f));
    CHECK(desk.scene().get<Collider>(box).parts[0].halfExtents == glm::vec3(0.5f));
    redo(desk.scene(), desk.state);
    CHECK_NEAR(desk.scene().get<Collider>(box).parts[0].halfExtents.x, 0.5f * drawn.back().x, 1e-5f);
}

TEST("a terrain stroke held over many frames is one undo step") {
    Game game;
    EditorHost& editor = EditorHost::on(game);
    openProject();
    Pointer pointer;
    TerrainToolOverlay brush;

    EntityId ground;
    TerrainHandle land;
    std::vector<float> flat, raised;
    game.run(16, [&](int frame) {
        Desk& desk = editor.desk();
        Pointer::aimAt(desk.ec);
        if (frame == 1) {
            ground = createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Terrain);
            desk.state.commands.endGesture();
            land = desk.scene().get<Terrain>(ground).terrain;
            flat = desk.resources().get(land).heights;
            desk.ec.cameraController.viewFrom({0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.3f}, 30.0f);
            desk.state.workspace.showTerrain = true;
            desk.state.terrainTool.tool      = TerrainTool::Raise;
            desk.state.terrainTool.target    = ground;
        }
        if (frame < 3) return;
        // Held from frame 3 to 12, the pointer drifting across the ground; let go at 13.
        pointer.moveTo(onScreen(desk.ec.frame, {static_cast<float>(frame - 8), 0.0f, 0.0f}));
        pointer.button(frame <= 12);
        pointer.frame([&] { brush.update(desk.ec); });
        if (frame == 13) raised = desk.resources().get(land).heights;
    }, 0);

    Desk& desk = editor.desk();
    REQUIRE(!raised.empty());
    CHECK(raised != flat);
    CHECK(*std::max_element(raised.begin(), raised.end()) > 0.0f);
    CHECK(desk.state.commands.undoDepth() == 2);
    undo(desk.scene(), desk.state);
    CHECK(desk.resources().get(land).heights == flat);
    redo(desk.scene(), desk.state);
    CHECK(desk.resources().get(land).heights == raised);
}

TEST("a click on a part of a placed thing selects the thing first, and the next click the part") {
    Scene scene;
    const EntityId house = scene.createEntity();
    const EntityId door  = scene.createEntity();
    const EntityId knob  = scene.createEntity();
    const EntityId loose = scene.createEntity();
    scene.add(house, PrefabInstance{});
    HierarchyOperations::setParent(scene, door, house);
    HierarchyOperations::setParent(scene, knob, door);

    CHECK(clickTarget(scene, knob, {}) == house);
    CHECK(clickTarget(scene, knob, house) == knob);
    CHECK(clickTarget(scene, knob, knob) == knob);
    CHECK(clickTarget(scene, door, knob) == door);
    // From another thing it starts at the outside again; an entity of no instance is itself.
    CHECK(clickTarget(scene, knob, loose) == house);
    CHECK(clickTarget(scene, loose, house) == loose);
}

TEST("a UI element's rect resizes from the side held, its body moves it to whole pixels") {
    UIElement start;  // its pivot at the center, 100 by 100
    const auto leftEdge  = [](const UIElement& e) { return e.position.x - e.pivot.x * e.size.x; };
    const auto rightEdge = [&](const UIElement& e) { return leftEdge(e) + e.size.x; };

    const UIElement wider = dragRect(start, RectHandle::Right, {20.0f, 0.0f});
    CHECK(wider.size.x == 120.0f);
    CHECK(leftEdge(wider) == leftEdge(start));
    const UIElement narrower = dragRect(start, RectHandle::Left, {30.0f, 0.0f});
    CHECK(narrower.size.x == 70.0f);
    CHECK(rightEdge(narrower) == rightEdge(start));
    CHECK(dragRect(start, RectHandle::BottomRight, {10.0f, 20.0f}).size == glm::vec2(110.0f, 120.0f));
    const UIElement moved = dragRect(start, RectHandle::Body, {3.4f, -7.6f});
    CHECK(moved.position == glm::vec2(3.0f, -8.0f));
    CHECK(moved.size == start.size);
    CHECK(dragRect(start, RectHandle::Left, {500.0f, 0.0f}).size.x == 1.0f);

    // Stretched across its parent, its size is an inset: it goes on past a pixel, never jumps to one.
    UIElement stretched;
    stretched.relativeSize = {1.0f, 0.0f};
    stretched.size.x       = -40.0f;
    CHECK(dragRect(stretched, RectHandle::Left, {10.0f, 0.0f}).size.x == -50.0f);
}

TEST("a snapped drag travels in whole steps from where it began, and a scale never snaps to zero") {
    CHECK_NEAR(snapTravel(0.26f, 0.1f), 0.3f, 1e-5f);
    CHECK(snapTravel(0.26f, 0.0f) == 0.26f);
    // A scale off the grid steps from where it began, so one of 0.01 held still stays 0.01.
    CHECK_NEAR(snapScale(0.25f, 0.37f, 0.1f), 0.35f, 1e-5f);
    CHECK_NEAR(snapScale(0.01f, 0.0101f, 0.1f), 0.01f, 1e-5f);
    // A step down never reaches zero, nor below one step from above it.
    CHECK(snapScale(0.3f, 0.003f, 0.1f) >= 0.1f - 1e-4f);
    CHECK(snapScale(0.25f, 0.02f, 0.1f) >= 0.1f - 1e-4f);
}
