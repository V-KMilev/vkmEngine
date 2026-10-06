#include "support.h"

#include <nlohmann/json.hpp>

#include "input/camera_controller_system.h"
#include "input/input_ownership.h"
#include "session/play_snapshot.h"

#include "ecs/component/render/camera.h"
#include "io/asset/asset_factory.h"
#include "io/asset/asset_library.h"
#include "io/asset/asset_serializer.h"
#include "io/scene/scene_serializer.h"
#include "resource/asset_source_kind.h"
#include "system/visibility/visibility.h"
#include "system/visibility/visibility_system.h"
#include "resource/generate/mesh_generators.h"

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
    const nlohmann::json paint = {{"kind", AssetSourceKind::INLINE}, {"roughness", 0.25f}};
    const nlohmann::json art   = {{"kind", AssetSourceKind::FILE}, {"path", "art.png"}};
    check(
        "the material's recipe writes",
        AssetLibrary::writeRecipe(AssetType::Material, "play:paint", paint)
    );
    check("  and the texture's", AssetLibrary::writeRecipe(AssetType::Texture, "play:art", art));
    AssetLibrary::get().upsert({AssetType::Material, "play:paint", 1u, {}});
    AssetLibrary::get().upsert({AssetType::Texture, "play:art", 2u, {}});

    ResourceManager resources;
    MaterialAsset painted;
    AssetSerializer::applyInline(paint, painted, resources);
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
    testASessionOwnsItsViewportsInput();
    testAFreedCursorIsStillOverTheViewport();
    testAStoodDownViewRefusesToMove();
    testAnEjectedSessionKeepsTheCursorFree();
    testAHandEditedPitchLimitIsHeldToItsSide();
    testTheEditorsViewLeavesTheSceneAlone();
    testPlayRendersThroughTheGameUnlessEjected();
}
