#include "support.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "editor_state.h"
#include "input/camera_controller_system.h"
#include "input/input_ownership.h"
#include "input/view_framing.h"
#include "input/view_placement.h"
#include "session/play_snapshot.h"
#include "session/scene_recovery.h"
#include "session/asset_writes.h"

#include "cook/asset_cooker.h"
#include "core/math/rotation.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/mesh.h"
#include "ecs/hierarchy_operations.h"
#include "io/asset/asset_factory.h"
#include "io/asset/asset_library.h"
#include "io/asset/asset_serializer.h"
#include "io/scene/scene_serializer.h"
#include "platform/window/glfw_include.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/terrain_asset.h"
#include "resource/asset_source_kind.h"
#include "resource/generate/mesh_generators.h"
#include "system/visibility/visibility.h"
#include "system/visibility/visibility_system.h"

namespace {

constexpr bool SCENE_SAVED = false;
constexpr bool SCENE_DIRTY = true;

// Play captures the world, the session changes it, Stop puts it back. It is the
// editor's most destructive operation, and PlaySnapshot holds all of it with no
// window behind it, so it is tested here whole.

void testPlayPutsTheWorldBack() {
    std::printf("What Stop restores:\n");

    Scene scene;
    ResourceManager resources;
    resources.add(generateCube(), "play:cube");

    const EntityId prop = scene.createEntity();
    Transform placed;
    placed.position = {1.0f, 2.0f, 3.0f};
    scene.add<Transform>(prop, std::move(placed));
    scene.add(prop, makeName("Prop"));

    PlaySnapshot snapshot;
    check("nothing is held before Play", !snapshot.held());
    check("Play captures the world", snapshot.capture(scene, resources, SCENE_SAVED));
    check("  and something is held now", snapshot.held());

    // The session moves, spawns and destroys things.
    scene.get<Transform>(prop).position = {99.0f, 99.0f, 99.0f};
    const EntityId spawned = scene.createEntity();
    scene.add<Transform>(spawned, Transform{});
    scene.add(spawned, makeName("Spawned"));

    check("Stop restores it", snapshot.restoreInto(scene, resources));

    // The world is the one Play found, not the one the session left.
    EntityId restored{};
    scene.forEach<Name>([&](EntityId id, Name& n) {
        if (std::string(n.value) == "Prop") restored = id;
    });
    check("  the entity that was there is there", bool(restored));
    check(
        "  at the position it was placed at",
        sameDirection(scene.get<Transform>(restored).position, {1.0f, 2.0f, 3.0f})
    );

    size_t spawnedSurvivors = 0;
    scene.forEach<Name>([&](EntityId, Name& n) {
        if (std::string(n.value) == "Spawned") ++spawnedSurvivors;
    });
    check("  and what the session spawned is gone", spawnedSurvivors == 0);
}

void testWhatTheSnapshotRemembersAboutTheSession() {
    std::printf("What Stop reads off the snapshot:\n");

    Scene scene;
    ResourceManager resources;
    scene.add<Transform>(scene.createEntity(), Transform{});

    PlaySnapshot snapshot;
    check("captured with the flag as it stood", snapshot.capture(scene, resources, SCENE_DIRTY));
    check("  and it remembers the flag", snapshot.dirtyAtCapture());

    snapshot.release();
    check("release ends the session", !snapshot.held());
    check("  and a released snapshot restores nothing", !snapshot.restoreInto(scene, resources));
}

// The prune keeps what the document names and drops the rest, so an absent section
// read as "names none of that kind" would take every asset of it. An older or
// truncated snapshot says nothing about a kind, not that there were none. The load
// half reads an absent section as nothing to do; this is the half that deletes.
void testASectionTheDocumentDoesNotMentionIsLeftAlone() {
    std::printf("What a snapshot that never mentions a kind may remove:\n");

    ResourceManager resources;
    resources.add(generateCube(), "authored:cube");
    MaterialAsset paint;
    resources.add(std::move(paint), "authored:material");

    // saveAllAssets's shape with one kind missing, as from a build with one fewer kind.
    nlohmann::json assets;
    assets["materials"] = nlohmann::json::array();
    assets["materials"].push_back({{"name", "authored:material"}});
    // No "meshes" key at all.

    const size_t dropped = AssetSerializer::dropAssetsNotIn(assets, resources);

    check(
        "a kind the document never mentions is not pruned",
        static_cast<bool>(resources.findByName<MeshAsset>("authored:cube"))
    );
    check(
        "  and the kind it does mention keeps what it names",
        static_cast<bool>(resources.findByName<MaterialAsset>("authored:material"))
    );
    check("  so nothing was dropped at all", dropped == 0);

    // Present and empty means this document names none of that kind: drop them.
    assets["meshes"] = nlohmann::json::array();
    check(
        "an empty section still prunes the kind it names",
        AssetSerializer::dropAssetsNotIn(assets, resources) == 1
    );
    check("  taking the mesh with it", !resources.findByName<MeshAsset>("authored:cube"));
}

// A project building its world at play time creates assets. Stop must take the
// graph back too, or every Play leaves a set behind in the Asset Browser and every
// picker, and a name still held would push the next Play's asset to a suffixed name.
void testStopTakesBackWhatTheSessionCreated() {
    std::printf("What Stop does to assets the session made:\n");

    Scene scene;
    ResourceManager resources;
    resources.add(generateCube(), "authored:cube");

    const EntityId prop = scene.createEntity();
    scene.add<Transform>(prop, Transform{});
    scene.add(prop, makeName("Prop"));

    PlaySnapshot snapshot;
    check("Play captures the graph as it stands", snapshot.capture(scene, resources, SCENE_SAVED));

    resources.add(generateCube(), "session:cube");
    MaterialAsset generated;
    resources.add(std::move(generated), "session:material");
    check(
        "the session's assets are in the graph",
        static_cast<bool>(resources.findByName<MeshAsset>("session:cube"))
            && static_cast<bool>(resources.findByName<MaterialAsset>("session:material"))
    );

    check("Stop restores", snapshot.restoreInto(scene, resources));

    check(
        "what the session created is gone",
        !resources.findByName<MeshAsset>("session:cube")
            && !resources.findByName<MaterialAsset>("session:material")
    );
    check(
        "  and what was authored before it is not",
        static_cast<bool>(resources.findByName<MeshAsset>("authored:cube"))
    );

    // The second Play shows it: without the drop the graph grows and names come
    // back suffixed.
    check(
        "so a second Play starts from the graph the first one found",
        snapshot.capture(scene, resources, false)
    );
    resources.add(generateCube(), "session:cube");
    check(
        "  and the name it generates is its own, not a suffixed duplicate",
        static_cast<bool>(resources.findByName<MeshAsset>("session:cube"))
            && !resources.findByName<MeshAsset>("session:cube (2)")
    );
}

// An import that answers with a stub still decoding.
TextureHandle importStillDecoding(const nlohmann::json&, ResourceManager& resources) {
    TextureAsset stub;
    stub.loading = true;
    return resources.add(std::move(stub), "import:decoding");
}

// Stop rebuilds materials the Material Editor may have changed, in their own slots,
// and leaves other kinds alone. A texture rebuilt through a still-decoding import
// would put the stub in its live slot while the decode landed on the discarded
// shell - loading for ever.
void testStopRebuildsMaterialsAndKeepsTheRest() {
    std::printf("What Stop rebuilds in place:\n");

    const ScratchProject project("vkm_play_reload");
    const auto previousTexture = assetFactory().createTexture;
    assetFactory().createTexture = &importStillDecoding;

    // Recorded and on disk, as Play's cook leaves them. The texture has no cooked
    // file, so its name resolves to the recipe and reaches the importer.
    const nlohmann::json paint = {{"roughness", 0.25f}};
    const nlohmann::json art   = {
        {AssetSourceKey::KIND, AssetSourceKind::FILE},
        {AssetSourceKey::PATH, "art.png"}
    };
    check(
        "the material's recipe writes",
        AssetLibrary::writeRecipe(AssetType::Material, "play:paint", paint)
    );
    check("  and the texture's", AssetLibrary::writeRecipe(AssetType::Texture, "play:art", art));
    AssetLibrary::get().upsert({AssetType::Material, "play:paint", 1u, {}});
    AssetLibrary::get().upsert({AssetType::Texture, "play:art", 2u, {}});

    ResourceManager resources;
    MaterialAsset painted = AssetSerializer::materialFromRecipe(paint, resources);
    const MaterialHandle material = resources.add(std::move(painted), "play:paint");
    TextureAsset loaded;
    loaded.pixelData = {1, 2, 3, 4};
    const TextureHandle texture = resources.add(std::move(loaded), "play:art");
    const nlohmann::json atPlay = AssetSerializer::saveAllAssets(resources);

    // The session: the Material Editor changes the paint.
    resources.edit(material).roughness = 0.9f;

    AssetSerializer::loadAssets(atPlay, resources, AssetSerializer::LoadMode::Reload);

    check("the material is what Play found", nearly(resources.get(material).roughness, 0.25f));
    check("  in the slot it was in", resources.findByName<MaterialAsset>("play:paint") == material);
    check(
        "the texture is left as it was",
        !resources.get(texture).loading && resources.get(texture).pixelData.size() == 4
    );
    check("  and nothing was imported over it", !resources.findByName<TextureAsset>("import:decoding"));

    assetFactory().createTexture = previousTexture;
    AssetLibrary::get().remove(AssetType::Material, "play:paint");
    AssetLibrary::get().remove(AssetType::Texture, "play:art");
}

// An asset the library holds and the session has not loaded comes in by name, as the Assets panel
// and an inspector slot bring one in when it is used; a name the library lacks does not.
void testALibraryAssetIsLoadedByName() {
    std::printf("A library asset, loaded by name:\n");

    const ScratchProject project("vkm_load_named");
    const nlohmann::json paint = {{"roughness", 0.3f}};
    check(
        "the material's recipe writes",
        AssetLibrary::writeRecipe(AssetType::Material, "named:paint", paint)
    );
    AssetLibrary::get().upsert({AssetType::Material, "named:paint", 1u, {}});

    ResourceManager resources;
    const bool loaded = AssetSerializer::loadNamed(AssetType::Material, "named:paint", resources);
    const MaterialHandle material = resources.findByName<MaterialAsset>("named:paint");
    check("it loads", loaded && material);
    check("  as its recipe says", material && nearly(resources.get(material).roughness, 0.3f));
    check(
        "  and loaded again is the one already held",
        AssetSerializer::loadNamed(AssetType::Material, "named:paint", resources)
            && resources.findByName<MaterialAsset>("named:paint") == material
    );
    check(
        "a name the library lacks does not load",
        !AssetSerializer::loadNamed(AssetType::Material, "named:absent", resources)
    );

    AssetLibrary::get().remove(AssetType::Material, "named:paint");
}

// A game may bind the editor's keys - Ctrl+D, F, Delete, Ctrl+Z, the right button.
// While a session runs with the pointer on the viewport, they are the game's alone.
void testASessionOwnsItsViewportsInput() {
    std::printf("Who has the input during a session:\n");

    InputSignals overScene;
    overScene.viewportHovered = true;

    InputSignals playing = overScene;
    playing.playing = true;
    const InputOwnership inSession = resolveInputOwnership(playing);
    check(
        "in a session over the viewport the game has the keyboard",
        inSession.gameHasKeyboard() && !inSession.editorHasKeys()
    );
    check(
        "  and the engine is told the host holds neither device",
        !inSession.hostHoldsPointer() && !inSession.hostHoldsKeyboard()
    );
    check("  and a left click is the game's, not a pick", !inSession.clickPicks());

    InputSignals ejectedOver = playing;
    ejectedOver.ejected = true;
    check("  until the session is ejected", resolveInputOwnership(ejectedOver).clickPicks());

    const InputOwnership editing = resolveInputOwnership(overScene);
    check(
        "outside one the editor's shortcuts act over the viewport",
        editing.editorHasKeys() && !editing.hostHoldsKeyboard()
    );
    check("  and the host holds the pointer, so the game's UI does not scroll", editing.hostHoldsPointer());

    InputSignals stroking = overScene;
    stroking.stroking = true;
    const InputOwnership held = resolveInputOwnership(stroking);
    check(
        "a held terrain stroke keeps every editor key from acting, undo and Play included",
        !held.editorHasKeys() && !held.editorHasSessionKeys()
    );
    InputSignals heldDrag = overScene;
    heldDrag.gizmoDragging = true;
    check("  as a held gizmo drag does", !resolveInputOwnership(heldDrag).editorHasKeys());

    InputSignals overPanel = playing;
    overPanel.viewportHovered = false;
    const InputOwnership onPanel = resolveInputOwnership(overPanel);
    check(
        "a session's pointer on a panel gives the panel the keyboard",
        onPanel.editorHasKeys() && onPanel.hostHoldsKeyboard() && onPanel.hostHoldsPointer()
    );

    InputSignals overPlaybar = playing;
    overPlaybar.overlayHovered = true;
    const InputOwnership onPlaybar = resolveInputOwnership(overPlaybar);
    check(
        "the playbar keeps its click in a session",
        onPlaybar.pointer == PointerOwner::Overlay && onPlaybar.hostHoldsPointer() && !onPlaybar.clickPicks()
    );

    // The game grabs the cursor; ImGui still hears it wander unseen, likely over a panel.
    InputSignals grabbed = overPanel;
    grabbed.cursorCaptured = true;
    const InputOwnership captured = resolveInputOwnership(grabbed);
    check(
        "a cursor the game has grabbed is the game's wherever it wandered",
        captured.pointer == PointerOwner::Captured
            && captured.gameHasKeyboard()
            && !captured.hostHoldsPointer()
            && !captured.hostHoldsKeyboard()
    );
    check(
        "  and nothing of the editor's answers its clicks",
        !captured.clickPicks() && !captured.gizmoMayHover() && !captured.navigationMayHover()
    );
    check(
        "  yet the transport keys still act, so the session can be left",
        captured.editorHasSessionKeys() && inSession.editorHasSessionKeys()
    );

    InputSignals onElement = playing;
    onElement.gameUIHovered = true;
    check(
        "a click on the game's UI is the game's in a session",
        !resolveInputOwnership(onElement).clickPicks()
    );
    onElement.playing = false;
    check("  and selects the element outside one", resolveInputOwnership(onElement).clickPicks());

    InputSignals dragging = overPanel;
    dragging.playing        = false;
    dragging.gizmoDragging  = true;
    check(
        "a gizmo drag keeps the pointer as it crosses a panel",
        resolveInputOwnership(dragging).pointer == PointerOwner::GizmoHandle
    );

    InputSignals typing = playing;
    typing.typing = true;
    check(
        "a field being typed into keeps the keyboard even over the viewport",
        resolveInputOwnership(typing).hostHoldsKeyboard()
    );
    check("  and the transport keys with it", !resolveInputOwnership(typing).editorHasSessionKeys());

    // A key pressed at a menu, dialog or rebind belongs to that UI, whichever reader
    // asks: the shortcuts, the transport, the toggle and the fly camera.
    InputSignals atPopup = overScene;
    atPopup.popupOpen = true;
    const InputOwnership popup = resolveInputOwnership(atPopup);
    check(
        "an open popup has the keyboard: no shortcut acts",
        !popup.editorHasKeys() && !popup.editorHasSessionKeys()
    );
    check("  and the fly camera hears none of it", popup.panelsHoldKeyboard());

    InputSignals atRebind = overScene;
    atRebind.rebinding = true;
    const InputOwnership rebind = resolveInputOwnership(atRebind);
    check(
        "a key pressed to rebind is the rebind's alone",
        !rebind.editorHasKeys() && !rebind.editorHasSessionKeys() && rebind.panelsHoldKeyboard()
    );
}

// The game frees a grabbed cursor with a key still held. While grabbed, ImGui hears
// no mouse and reports nothing hovered; read as a panel, the first free frame would
// file the held key as the host's.
void testAFreedCursorIsStillOverTheViewport() {
    std::printf("Who has the input the frame a game frees the cursor:\n");

    InputSignals signals;
    signals.playing         = true;
    signals.viewportHovered = true;
    check("the game has the keyboard over its viewport", resolveInputOwnership(signals).gameHasKeyboard());

    signals.cursorCaptured = true;
    const InputOwnership grabbed = resolveInputOwnership(signals);
    signals.viewportHovered = nextViewportHover(signals.viewportHovered, false, grabbed);

    signals.cursorCaptured = false;
    const InputOwnership freed = resolveInputOwnership(signals);
    check("freed, the pointer is where it was grabbed", freed.pointer == PointerOwner::Scene);
    check(
        "  so the game keeps the keyboard and the host claims none of it",
        freed.gameHasKeyboard() && !freed.hostHoldsKeyboard() && !freed.hostHoldsPointer()
    );

    signals.viewportHovered = nextViewportHover(signals.viewportHovered, false, freed);
    check(
        "a free cursor that leaves the viewport is believed",
        resolveInputOwnership(signals).pointer == PointerOwner::Panel
    );
}

// The editor's view is its own: looking around must write nothing the scene file
// stores, or every look would mark the scene unsaved.
void testTheEditorsViewLeavesTheSceneAlone() {
    std::printf("What the editor's view writes:\n");

    Scene scene;
    TestFrame frame(scene);
    const EntityId game = scene.createEntity();
    Transform at;
    at.position = {1.0f, 2.0f, 3.0f};
    scene.add(game, std::move(at));
    scene.add(game, Camera{});
    const std::string before = SceneSerializer::saveToString(scene, frame.resources);

    CameraControllerSystem controller;
    controller.startFrom(scene);
    check(
        "a scene is first seen from where its camera stands",
        controller.viewpoint().position == glm::vec3(1.0f, 2.0f, 3.0f)
    );

    controller.update(frame.ctx);
    controller.focusOn(glm::vec3(10.0f, 0.0f, 0.0f), 4.0f);
    controller.viewFrom(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 1.0f), 8.0f);
    frame.ctx.hostView = nullptr;
    controller.update(frame.ctx);

    check(
        "a view moved in Edit mode leaves the scene as it was saved",
        SceneSerializer::saveToString(scene, frame.resources) == before
    );
    const glm::vec3 framedFrom = glm::vec3(0.0f, 8.0f, 8.0f) / glm::sqrt(2.0f);
    check(
        "  and is offered to the frame instead",
        frame.ctx.hostView
            && !frame.ctx.hostView->through
            && frame.ctx.hostView->position == controller.viewpoint().position
            && glm::length(controller.viewpoint().position - framedFrom) < 1e-4f
    );

    // A scene with no camera is seen from a default.
    Scene bare;
    controller.startFrom(bare);
    check("a scene with no camera is still framed", controller.viewpoint().pitch < 0.0f);
}

// The editor's view moves by the gestures every editor shares: Alt and the left
// button orbit the focus, the middle button pans, the wheel dollies or, looking,
// sets how fast the keys fly.
void testTheEditorsViewOrbitsPansAndDollies() {
    std::printf("How the editor's view is steered:\n");

    Scene scene;
    TestFrame frame(scene);
    InputHandle& hand = frame.window.getInputHandle();
    CameraControllerSystem controller;
    const glm::vec3 focus(1.0f, 0.0f, -2.0f);
    controller.viewFrom(focus, glm::vec3(0.0f, 0.0f, 1.0f), 10.0f);

    // One frame: the strikes and the pointer's move are the frame's alone.
    double x = 0.0;
    const auto step = [&](double move) {
        x += move;
        hand.moveTo(x, 0.0);
        controller.update(frame.ctx);
        hand.clearStrikes();
        hand.resetScrollDelta();
    };
    const auto distance = [&] { return glm::length(controller.viewpoint().position - focus); };
    const auto forward = [&] {
        const EditorViewpoint& view = controller.viewpoint();
        return Math::computeForward(Math::fromYawPitch(view.yaw, view.pitch));
    };

    hand.onKeyEvent(GLFW_KEY_LEFT_ALT, true);
    hand.setButton(GLFW_MOUSE_BUTTON_LEFT, true);
    step(200.0);
    step(100.0);
    check(
        "an orbit turns the view about its focus",
        controller.isOrbiting() && std::abs(controller.viewpoint().yaw) > 0.1f
            && std::abs(distance() - 10.0f) < 1e-3f
            && sameDirection(controller.viewpoint().position + forward() * 10.0f, focus)
    );
    check("  and holds the cursor", frame.window.cursorMode() == CursorMode::Disabled);
    hand.setButton(GLFW_MOUSE_BUTTON_LEFT, false);
    hand.onKeyEvent(GLFW_KEY_LEFT_ALT, false);
    step(0.0);
    check(
        "let go, the cursor is given back",
        !controller.isNavigating() && frame.window.cursorMode() == CursorMode::Normal
    );

    // A left drag begun without Alt is a pick's; Alt pressed during it turns nothing.
    const float yaw = controller.viewpoint().yaw;
    hand.setButton(GLFW_MOUSE_BUTTON_LEFT, true);
    step(0.0);
    hand.onKeyEvent(GLFW_KEY_LEFT_ALT, true);
    step(150.0);
    check(
        "a left drag begun without Alt never orbits",
        !controller.isNavigating() && controller.viewpoint().yaw == yaw
    );
    hand.setButton(GLFW_MOUSE_BUTTON_LEFT, false);
    hand.onKeyEvent(GLFW_KEY_LEFT_ALT, false);
    step(0.0);

    const glm::vec3 before = controller.viewpoint().position;
    hand.setButton(GLFW_MOUSE_BUTTON_MIDDLE, true);
    step(0.0);
    step(-50.0);
    const glm::vec3 slid = controller.viewpoint().position - before;
    check(
        "a pan slides the view across, never nearer",
        controller.isNavigating() && glm::length(slid) > 0.01f && std::abs(glm::dot(slid, forward())) < 1e-4f
            && controller.viewpoint().yaw == yaw
    );
    hand.setButton(GLFW_MOUSE_BUTTON_MIDDLE, false);
    step(0.0);

    const glm::vec3 panned = controller.viewpoint().position + forward() * 10.0f;
    hand.addScroll(1.0);
    step(0.0);
    check(
        "the wheel dollies a share of the way to the focus",
        std::abs(glm::length(controller.viewpoint().position - panned) - 8.5f) < 1e-3f
    );

    const glm::vec3 dollied = controller.viewpoint().position;
    hand.setButton(GLFW_MOUSE_BUTTON_RIGHT, true);
    step(0.0);
    hand.addScroll(2.0);
    step(0.0);
    check(
        "looking, the wheel sets the fly speed instead",
        std::abs(controller.speedScale() - 1.5625f) < 1e-4f && controller.viewpoint().position == dollied
            && controller.speedShownFor() > 0.0f
    );
    hand.setButton(GLFW_MOUSE_BUTTON_RIGHT, false);
    step(0.0);

    // Orthographic, a notch zooms by the same share of what is framed, however close.
    controller.setOrthographic(true);
    const float framed = controller.viewpoint().orthoHeight;
    hand.addScroll(1.0);
    step(0.0);
    const float once = controller.viewpoint().orthoHeight;
    hand.addScroll(1.0);
    step(0.0);
    const float twice = controller.viewpoint().orthoHeight;
    check(
        "orthographic, each notch zooms by a share",
        std::abs(once - framed * 0.85f) < 1e-4f && std::abs(twice - once * 0.85f) < 1e-4f
    );
}

// Create puts a new entity where the author is looking, not at the origin they may
// be far from: on the surface the view meets, else on the ground, else ahead.
void testANewEntityGoesWhereTheViewLooks() {
    std::printf("Where a new entity goes:\n");

    Scene scene;
    ResourceManager resources;
    const MeshHandle cube = addGeneratedMesh(resources, generateCube());
    const EntityId floor = scene.createEntity();
    Transform slab;
    slab.position = {0.0f, 0.5f, -10.0f};
    slab.scale    = {20.0f, 1.0f, 20.0f};
    scene.add(floor, std::move(slab));
    scene.add(floor, Mesh{cube, {}});

    ViewAim aim;
    aim.ray = {glm::vec3(0.0f, 5.0f, 0.0f), glm::normalize(glm::vec3(0.0f, -1.0f, -1.0f))};
    check(
        "before the view is known, the origin",
        ViewPlacement::placeInView(scene, resources, aim, 0.5f) == glm::vec3(0.0f)
    );

    aim.known = true;
    const glm::vec3 onTop = ViewPlacement::placeInView(scene, resources, aim, 0.5f);
    check(
        "where the view meets a surface, standing on it",
        glm::length(onTop - glm::vec3(0.0f, 1.5f, -4.0f)) < 1e-3f
    );
    EntityId met{};
    float    t = 0.0f;
    check(
        "  and the surface met is named, for a drop to land on",
        ViewPlacement::firstSurface(scene, resources, aim.ray, t, {}, &met) && met == floor
    );

    scene.get<Mesh>(floor).visible = false;
    const glm::vec3 onGround = ViewPlacement::placeInView(scene, resources, aim, 0.5f);
    check(
        "a hidden mesh is not met; the ground is",
        glm::length(onGround - glm::vec3(0.0f, 0.5f, -5.0f)) < 1e-3f
    );

    aim.ray.direction = {0.0f, 0.0f, -1.0f};
    aim.focus         = 100.0f;
    const glm::vec3 ahead = ViewPlacement::placeInView(scene, resources, aim, 0.5f);
    check(
        "meeting nothing, ahead of the view, no further than it can see well",
        glm::length(ahead - glm::vec3(0.0f, 5.0f, -ViewPlacement::MAX_DISTANCE)) < 1e-3f
    );
    check(
        "a mesh is raised by how far it reaches below its origin; no mesh by nothing",
        ViewPlacement::meshBase(resources, cube) == 0.5f && ViewPlacement::meshBase(resources, {}) == 0.0f
    );
}

// Frame Selected shows the whole selection and what hangs under it, not the active
// entity's own mesh alone.
void testFrameSelectedShowsTheWholeSelection() {
    std::printf("What Frame Selected shows:\n");

    Scene scene;
    TestFrame frame(scene);
    const MeshHandle cube = addGeneratedMesh(frame.resources, generateCube());
    const auto placed = [&](const glm::vec3& at, bool withMesh) {
        const EntityId id = scene.createEntity();
        Transform pose;
        pose.position = at;
        scene.add(id, std::move(pose));
        if (withMesh) scene.add(id, Mesh{cube, {}});
        return id;
    };
    const EntityId left  = placed({-10.0f, 0.0f, 0.0f}, true);
    const EntityId right = placed({10.0f, 0.0f, 0.0f}, false);
    const EntityId hung  = placed({0.0f, 0.0f, 0.0f}, true);
    HierarchyOperations::setParent(scene, hung, right);  // at the parent's origin: (10, 0, 0)

    CameraControllerSystem controller;
    controller.viewFrom(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 5.0f);
    ViewFraming::frameSelected(frame.ctx, {left, right}, controller);

    // The union spans x from -10.5 to 10.5: its centre is the origin, its half-diagonal ~10.5.
    const EditorViewpoint& view = controller.viewpoint();
    const glm::vec3 forward = Math::computeForward(Math::fromYawPitch(view.yaw, view.pitch));
    const glm::vec3 focus   = view.position + forward * controller.focusDistance();
    check("it centres on the selection together", glm::length(focus) < 1e-3f);
    const float halfView = std::tan(controller.fieldOfView() * 0.5f) * controller.focusDistance();
    check(
        "  near enough back to hold all of it, the child's mesh included",
        halfView >= 10.5f && controller.focusDistance() < 40.0f
    );

    ViewFraming::frameSelected(frame.ctx, {hung}, controller);
    const glm::vec3 closeIn = controller.viewpoint().position
        + Math::computeForward(Math::fromYawPitch(controller.viewpoint().yaw, controller.viewpoint().pitch))
            * controller.focusDistance();
    check(
        "one small mesh is framed close, where its parent put it",
        glm::length(closeIn - glm::vec3(10.0f, 0.0f, 0.0f)) < 1e-3f && controller.focusDistance() < 5.0f
    );
}

// Drop to Ground lets a thing fall onto what is under it - never onto itself - and
// lifts it out of a floor it is sunk in.
void testADroppedEntityRestsOnWhatIsBelow() {
    std::printf("Where Drop to Ground lets an entity fall:\n");

    Scene scene;
    ResourceManager resources;
    const MeshHandle cube = addGeneratedMesh(resources, generateCube());
    const auto placed = [&](const glm::vec3& at, const glm::vec3& size) {
        const EntityId id = scene.createEntity();
        Transform pose;
        pose.position = at;
        pose.scale    = size;
        scene.add(id, std::move(pose));
        scene.add(id, Mesh{cube, {}});
        return id;
    };
    placed({0.0f, -0.5f, 0.0f}, {10.0f, 1.0f, 10.0f});  // a floor whose top is y = 0
    const EntityId crate = placed({0.0f, 3.0f, 0.0f}, glm::vec3(1.0f));

    float drop = 0.0f;
    check(
        "a crate above the floor falls until its base is on it",
        ViewPlacement::dropBelow(scene, resources, crate, drop) && std::abs(drop - 2.5f) < 1e-3f
    );
    scene.get<Transform>(crate).position.y = 0.4f;
    check(
        "  and one sunk in it rises out",
        ViewPlacement::dropBelow(scene, resources, crate, drop) && std::abs(drop + 0.1f) < 1e-3f
    );
    scene.get<Transform>(crate).position = {50.0f, 3.0f, 0.0f};
    check(
        "off the floor, the ground plane catches it",
        ViewPlacement::dropBelow(scene, resources, crate, drop) && std::abs(drop - 2.5f) < 1e-3f
    );
    scene.get<Transform>(crate).position = {50.0f, -3.0f, 0.0f};
    check(
        "below the ground with nothing under it, it does not move",
        !ViewPlacement::dropBelow(scene, resources, crate, drop)
    );

    // A tall child under a small parent: the base is the child's, and neither is the floor.
    scene.get<Transform>(crate).position = {0.0f, 5.0f, 0.0f};
    const EntityId leg = placed({0.0f, -1.0f, 0.0f}, {0.2f, 2.0f, 0.2f});
    HierarchyOperations::setParent(scene, leg, crate);
    check(
        "what hangs under it lands, not the parent's own base",
        ViewPlacement::dropBelow(scene, resources, crate, drop) && std::abs(drop - 3.0f) < 1e-3f
    );

    // Moved this frame, before any transform pass: a surface is met where it is now.
    scene.get<Transform>(crate).position = {20.0f, 5.0f, 0.0f};
    const Math::Ray down{glm::vec3(20.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)};
    float t = 0.0f;
    EntityId met{};
    check(
        "a body just moved is met where it went",
        ViewPlacement::firstSurface(scene, resources, down, t, {}, &met) && (met == crate || met == leg)
    );
}

// Align View to Selected stands the view where a camera stands, to look through it.
void testTheViewStandsWhereAnEntityStands() {
    std::printf("What Align View to Selected does:\n");

    Scene scene;
    const EntityId lens = scene.createEntity();
    Transform pose;
    pose.position = {3.0f, 4.0f, 5.0f};
    pose.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    scene.add(lens, std::move(pose));

    CameraControllerSystem controller;
    ViewFraming::viewFromEntity(scene, lens, controller);
    const EditorViewpoint& view = controller.viewpoint();
    const glm::vec3 looking = Math::computeForward(Math::fromYawPitch(view.yaw, view.pitch));
    check(
        "the view stands there and looks along its forward",
        view.position == glm::vec3(3.0f, 4.0f, 5.0f) && sameDirection(looking, glm::vec3(-1.0f, 0.0f, 0.0f))
    );
}

// What a crash leaves of unsaved changes is found again by its scene, and only while it
// is newer than what the scene's file holds.
void testACrashsCopyIsFoundByItsScene() {
    std::printf("A copy of unsaved changes:\n");
    namespace fs = std::filesystem;

    const fs::path root = runScratch() / "vkm_recovery_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "project" / "scenes", ec);
    const fs::path    dir   = root / "recovery";
    const fs::path    proj  = root / "project";
    const std::string scene = (proj / "scenes" / "level.json").string();

    const fs::path copy = SceneRecovery::pathFor(dir, proj, scene);
    check(
        "each scene has its own, and a scene never saved one by its project",
        copy == SceneRecovery::pathFor(dir, proj, scene)
            && copy != SceneRecovery::pathFor(dir, proj, (proj / "scenes" / "other.json").string())
            && SceneRecovery::pathFor(dir, proj, {}) != SceneRecovery::pathFor(dir, root / "elsewhere", {})
    );
    check("with none kept, nothing is offered", !SceneRecovery::hasNewer(copy, scene));

    fs::create_directories(dir, ec);
    std::ofstream(copy) << "{}";
    check("a scene with no file has only its copy, so it is offered", SceneRecovery::hasNewer(copy, scene));

    std::ofstream(scene) << "{}";
    const fs::file_time_type now = fs::last_write_time(scene, ec);
    fs::last_write_time(copy, now - std::chrono::hours(1), ec);
    check("a copy older than the saved file is not", !SceneRecovery::hasNewer(copy, scene));
    fs::last_write_time(copy, now + std::chrono::hours(1), ec);
    check("  and a newer one is", SceneRecovery::hasNewer(copy, scene));

    fs::create_directories(SceneRecovery::terrainsOf(copy), ec);
    SceneRecovery::discard(copy);
    check("discarded, it is gone", !fs::exists(copy, ec));
    check("  and the terrains it kept with it", !fs::exists(SceneRecovery::terrainsOf(copy), ec));
    fs::remove_all(root, ec);
}

// A crash's copy keeps what the author has not saved out of the project: a recorded material
// stays in the library as it was saved, however it is edited, while a new one is recorded so the
// copy's name for it resolves.
void testACrashsCopyRewritesNothingTheProjectHolds() {
    std::printf("What a crash's copy writes into the project:\n");

    const ScratchProject project("vkm_recovery_assets");
    ResourceManager      resources;
    const MaterialHandle paint = resources.add(MaterialAsset{}, "kept:paint");
    check("the material is saved into the library", AssetCooker::cookAllAssets(resources));
    nlohmann::json saved;
    check("  as its recipe", AssetLibrary::readRecipe(AssetType::Material, "kept:paint", saved));

    resources.edit(paint).roughness = 0.9f;
    resources.add(MaterialAsset{}, "kept:new");
    const AssetWrites kept = keepAssets(resources, project.root() / "kept");
    check("the copy's assets are kept", kept.terrains && kept.cooked);
    nlohmann::json after;
    check(
        "the edited material's recipe is as the author saved it",
        AssetLibrary::readRecipe(AssetType::Material, "kept:paint", after) && after == saved
    );
    check(
        "  and a new one is recorded, so the copy finds it",
        AssetLibrary::get().find(AssetType::Material, "kept:new") != nullptr
    );

    AssetLibrary::get().remove(AssetType::Material, "kept:paint");
    AssetLibrary::get().remove(AssetType::Material, "kept:new");
}

// A terrain that cannot be written does not stop the cook beside it, and each half is reported.
void testATerrainNotWrittenDoesNotStopTheCook() {
    std::printf("A save's assets, with a terrain that cannot be written:\n");

    const ScratchProject project("vkm_asset_writes");
    // A file where the terrain's folder would go.
    std::ofstream(project.root() / "blocked") << "not a folder";
    ResourceManager resources;
    TerrainAsset    terrain;
    terrain.resolution = 33;
    terrain.size       = 32.0f;
    terrain.heights.assign(33 * 33, 0.0f);
    terrain.weights.assign(33 * 33 * 4, 0);
    resources.add(std::move(terrain), "blocked/ground.terrain");
    resources.add(MaterialAsset{}, "writes:paint");

    const AssetWrites writes = writeAssets(resources);
    check("the terrain is reported not written", !writes.terrains);
    check(
        "  and the material is cooked all the same",
        writes.cooked && AssetLibrary::get().find(AssetType::Material, "writes:paint") != nullptr
    );

    AssetLibrary::get().remove(AssetType::Material, "writes:paint");
}

// A settings file is hand-editable, and the pitch clamp is undefined with the low
// limit above the high.
void testAHandEditedPitchLimitIsHeldToItsSide() {
    std::printf("What a hand-edited pitch limit loads as:\n");

    CameraControllerSystem::Settings swapped;
    swapped.minPitch = 60.0f;
    swapped.maxPitch = -60.0f;
    const CameraControllerSystem::Settings held = CameraControllerSystem::Settings::bounded(swapped);
    check(
        "each limit is held to its own side of the horizon",
        held.minPitch <= 0.0f && held.maxPitch >= 0.0f
    );

    // A field the window bounds is held to what the window offers.
    Preferences wild;
    wild.camera.dollyStep       = 3.0f;
    wild.camera.lookSensitivity = 0.0f;
    wild.snapRotate             = 0.0f;
    const Preferences tame = Preferences::bounded(wild);
    check(
        "a hand-edited dolly, look or snap is held to what the window offers",
        tame.camera.dollyStep == CameraControllerSystem::Settings::MAX_DOLLY_STEP
            && tame.camera.lookSensitivity == CameraControllerSystem::Settings::MIN_LOOK
            && tame.snapRotate == Preferences::MIN_SNAP_ROTATE
    );

    CameraControllerSystem::Settings past;
    past.minPitch = -400.0f;
    past.maxPitch = 400.0f;
    const CameraControllerSystem::Settings straight = CameraControllerSystem::Settings::bounded(past);
    check(
        "  and no further than straight down or up",
        straight.minPitch == -CameraControllerSystem::Settings::PITCH_LIMIT
            && straight.maxPitch == CameraControllerSystem::Settings::PITCH_LIMIT
    );
}

// An ejected session shows the editor's view of a running game, and the panels need
// a free cursor. While ejected, a game's cursor grab is undone and remembered; the
// view returning hands the game what it last asked for.
void testAnEjectedSessionKeepsTheCursorFree() {
    std::printf("What an ejection does to the cursor:\n");

    Scene scene;
    ResourceManager resources;
    WindowManager window;
    scene.add<Transform>(scene.createEntity(), Transform{});

    PlaySnapshot session;
    session.setEjected(true, window);
    check("nothing ejects outside a session", !session.ejected());

    check("Play captures", session.capture(scene, resources, SCENE_SAVED));
    window.setCursorMode(CursorMode::Disabled);
    session.setEjected(true, window);
    check(
        "ejecting frees the cursor the game grabbed",
        session.ejected() && window.cursorMode() == CursorMode::Normal
    );

    window.setCursorMode(CursorMode::Captured);
    session.holdCursorFree(window);
    check("a grab while ejected is undone", window.cursorMode() == CursorMode::Normal);

    session.setEjected(false, window);
    check(
        "returning hands back what the game last asked for",
        !session.ejected() && window.cursorMode() == CursorMode::Captured
    );

    session.setEjected(true, window);
    session.release();
    check("a session's end ends the ejection", !session.ejected());
    window.setCursorMode(CursorMode::Disabled);
    session.holdCursorFree(window);
    check(
        "  and leaves the cursor to the editor's own view after it",
        window.cursorMode() == CursorMode::Disabled
    );
}

// The view moves through focusOn and viewFrom, so a view stood down for a session
// is held still there, once, whatever asked.
void testAStoodDownViewRefusesToMove() {
    std::printf("What moves a view stood down for a session:\n");

    CameraControllerSystem controller;
    const glm::vec3 start = controller.viewpoint().position;
    controller.setActive(false);
    controller.focusOn(glm::vec3(10.0f, 0.0f, 0.0f), 4.0f);
    controller.viewFrom(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 8.0f);
    check(
        "nothing: framing a stood-down view leaves it where it was",
        controller.viewpoint().position == start
    );

    controller.setActive(true);
    controller.focusOn(glm::vec3(10.0f, 0.0f, 0.0f), 4.0f);
    check("  and the view the editor has back frames again", controller.viewpoint().position != start);
}

// The viewport shows the game's camera during a session unless the author ejected,
// and the view the editor flies is never the game's.
void testPlayRendersThroughTheGameUnlessEjected() {
    std::printf("What a session's viewport renders through:\n");

    Scene scene;
    TestFrame frame(scene);
    const EntityId game = scene.createEntity();
    scene.add(game, Transform{});
    scene.add(game, Camera{});

    CameraControllerSystem controller;
    VisibilitySystem visibility;
    controller.setViewpoint(EditorViewpoint{glm::vec3(5.0f, 5.0f, 5.0f), 0.0f, 0.0f});

    // A frame: the context's products start null, as Engine::run rebuilds it.
    const auto runFrame = [&] {
        frame.ctx.hostView = nullptr;
        controller.update(frame.ctx);
        visibility.update(frame.ctx);
    };

    InputSignals playing;
    playing.viewportHovered = true;
    playing.playing = true;
    controller.setActive(!resolveInputOwnership(playing).gameHasViewport());
    runFrame();
    check(
        "in a session the frame renders through the game's camera",
        !frame.ctx.hostView && frame.ctx.visibility->cameraEntity == game
    );

    InputSignals ejected = playing;
    ejected.ejected = true;
    const InputOwnership out = resolveInputOwnership(ejected);
    controller.setActive(!out.gameHasViewport());
    runFrame();
    check(
        "ejected, it renders through the editor's view",
        frame.ctx.visibility->hasCamera
            && !frame.ctx.visibility->cameraEntity
            && frame.ctx.visibility->camera.position == glm::vec3(5.0f)
    );
    check(
        "  while the game is told the host holds both devices",
        out.hostHoldsPointer() && out.hostHoldsKeyboard()
    );
    check("  and the fly camera is not", !out.panelsHoldPointer() && !out.panelsHoldKeyboard());
    check(
        "  and the editor's shortcuts and picking act as in Edit mode",
        out.editorHasKeys() && out.clickPicks()
    );

    controller.setActive(!resolveInputOwnership(playing).gameHasViewport());
    runFrame();
    check("returning gives the game its camera back", frame.ctx.visibility->cameraEntity == game);

    InputSignals stopped = ejected;
    stopped.playing = false;
    check("an ejection outlives no session", !resolveInputOwnership(stopped).ejected);
}

} // namespace

void runPlayTests() {
    testPlayPutsTheWorldBack();
    testStopTakesBackWhatTheSessionCreated();
    testASectionTheDocumentDoesNotMentionIsLeftAlone();
    testWhatTheSnapshotRemembersAboutTheSession();
    testStopRebuildsMaterialsAndKeepsTheRest();
    testALibraryAssetIsLoadedByName();
    testASessionOwnsItsViewportsInput();
    testAFreedCursorIsStillOverTheViewport();
    testAStoodDownViewRefusesToMove();
    testAnEjectedSessionKeepsTheCursorFree();
    testAHandEditedPitchLimitIsHeldToItsSide();
    testTheEditorsViewLeavesTheSceneAlone();
    testTheEditorsViewOrbitsPansAndDollies();
    testANewEntityGoesWhereTheViewLooks();
    testFrameSelectedShowsTheWholeSelection();
    testADroppedEntityRestsOnWhatIsBelow();
    testTheViewStandsWhereAnEntityStands();
    testACrashsCopyIsFoundByItsScene();
    testACrashsCopyRewritesNothingTheProjectHolds();
    testATerrainNotWrittenDoesNotStopTheCook();
    testPlayRendersThroughTheGameUnlessEjected();
}
