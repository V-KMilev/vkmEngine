// A play session: Play runs the game on the scene as it stands, and Stop puts the authored scene back
// as Play found it. Meanwhile the session's undo is its own and nothing is saved.

#include <filesystem>
#include <string>
#include <vector>

#include "actions/asset_actions.h"
#include "actions/component_actions.h"
#include "actions/create_entity.h"
#include "actions/edit_actions.h"
#include "actions/entity_clipboard.h"
#include "core/graphics.h"
#include "ecs/component/landscape/terrain.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/scene/hierarchy.h"
#include "io/asset/terrain_file.h"
#include "io/project/roots.h"
#include "io/scene/scene_serializer.h"
#include "overlays/playback_bar.h"
#include "resource/asset/terrain_asset.h"
#include "session/asset/terrain_files.h"
#include "session/scene_actions.h"
#include "system/script/behavior.h"
#include "system/script/module/script_module.h"
#include "system/script/script_component.h"
#include "undo/component_edit.h"

#include "editor/desk.h"
#include "harness/check.h"

namespace EditorPlay {

/**
 * @brief Counts the game's ticks: each onFixedUpdate and onUpdate.
 */
class Ticks : public Vkm::Engine::Behavior<Ticks> {
    public:
        void onFixedUpdate(float) override { ++counted; }
        void onUpdate(float) override      { ++counted; }

    public:
        static inline int counted = 0;
};

} // namespace EditorPlay

VKM_BEHAVIOR(::EditorPlay::Ticks)
VKM_REFLECT_END()

using namespace Vkm::Test;
using namespace Vkm::Engine::EditorActions;
using namespace Vkm::Platform;
using EditorPlay::Ticks;

namespace {

void moveTo(Desk& desk, EntityId id, const glm::vec3& at) {
    {
        EditScope<Transform> pose(desk.scene(), desk.resources(), desk.state, id, "Edit Transform");
        pose->position = at;
    }
    desk.state.commands.endGesture();
}

/// Press Play / Stop by its key, as the author does: the playbar answers it on the frame it is down.
void pressPlayStop(Pointer& pointer, PlaybackBar& bar, Desk& desk) {
    pointer.key(ImGuiMod_Ctrl, true);
    pointer.key(ImGuiKey_P, true);
    pointer.frame([&] { bar.processKeys(desk.ec); });
    pointer.key(ImGuiKey_P, false);
    pointer.key(ImGuiMod_Ctrl, false);
    pointer.frame([] {});
}

} // namespace

TEST("Stop puts the scene, its materials and its terrains back as Play found them, with the history and selection") {
    Game game;
    EditorHost& editor = EditorHost::on(game);
    openProject();

    EntityId crate, ground, made;
    MaterialHandle paint;
    TerrainHandle land;
    std::string authored;
    std::vector<float> heights;
    size_t depth = 0;
    float roughness = 0.0f;
    float fell      = 0.0f;
    bool played = false;
    game.run(40, [&](int frame) {
        Desk& desk = editor.desk();
        Scene& scene = desk.scene();
        ResourceManager& resources = desk.resources();
        if (frame == 1) {
            // A crate on a material of its own, over the ground, each made as the author makes them.
            crate = createEntity(scene, resources, desk.state, EntityKind::Cube);
            moveTo(desk, crate, {0.0f, 3.0f, 0.0f});
            addComponent(scene, desk.state, crate, Rigidbody{}, "Rigidbody", "Add Rigidbody");
            addComponent(scene, desk.state, crate, Collider{}, "Collider", "Add Collider");
            paint  = forkMaterial(scene, resources, desk.state, crate, scene.get<Mesh>(crate).material);
            ground = createEntity(scene, resources, desk.state, EntityKind::Terrain);
            desk.state.commands.endGesture();
            desk.state.selection.select(crate);
            land      = scene.get<Terrain>(ground).terrain;
            heights   = resources.get(land).heights;
            roughness = resources.get(paint).roughness;
            depth     = desk.state.commands.undoDepth();
            authored  = desk.sceneText();
            played    = desk.startPlay();
        }
        if (frame >= 1 && frame < 30) game.clock().requestStep(1);
        if (frame == 10) {
            // What a game and an author in play do: tune the material, dig, move and make things.
            resources.edit(paint).roughness = 0.95f;
            resources.commit(paint);
            resources.edit(land).heights[0] += 2.0f;
            resources.commit(land);
            moveTo(desk, ground, {0.0f, -1.0f, 0.0f});
            made = createEntity(scene, resources, desk.state, EntityKind::Sphere);
        }
        if (frame == 30) {
            fell = scene.get<Transform>(crate).position.y;
            desk.stop();
        }
    }, 0);

    Desk& desk = editor.desk();
    REQUIRE(played);
    CHECK(fell < 2.5f);
    CHECK(!desk.play.isPlaying());
    CHECK(changes(authored, desk.sceneText()) == "");
    CHECK(!desk.scene().isAliveAtIndex(made.slot()));
    CHECK(desk.resources().isAlive(paint));
    CHECK(desk.resources().get(paint).roughness == roughness);
    CHECK(desk.resources().get(land).heights == heights);
    CHECK(desk.state.selection.active() == crate);
    CHECK(desk.said("The edits made during play were discarded"));

    // The authored history is back, and reaches the authored scene.
    CHECK(desk.state.commands.undoDepth() == depth);
    undo(desk.scene(), desk.state);
    CHECK(!desk.scene().isAliveAtIndex(ground.slot()));
}

TEST("Stop puts every entity back under the id it had at Play, the selection with it") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    // Slots used before, so each id is past its slot's first generation, and one left free.
    for (int made = 0; made < 3; ++made) {
        deleteEntity(scene, desk.state, createEntity(scene, desk.resources(), desk.state, EntityKind::Cube));
    }
    const EntityId crate = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    const EntityId ball  = createEntity(scene, desk.resources(), desk.state, EntityKind::Sphere);
    const EntityId gone  = createEntity(scene, desk.resources(), desk.state, EntityKind::Sphere);
    deleteEntity(scene, desk.state, gone);
    desk.state.commands.endGesture();
    desk.state.selection.select(ball);
    desk.state.selection.add(crate);
    const std::vector<EntityId> atPlay = entityIds(scene);
    REQUIRE(crate.generation() > 0);

    // The game takes the crate's slot for something of its own.
    REQUIRE(desk.startPlay());
    scene.destroyEntity(crate);
    scene.createEntity();
    desk.stop();

    CHECK(entityIds(scene) == atPlay);
    CHECK(desk.state.selection.entities().size() == 2);
    CHECK(desk.state.selection.active() == crate);
    // A slot free at Play hands out no id it gave before.
    CHECK(createEntity(scene, desk.resources(), desk.state, EntityKind::Cube) != gone);
}

TEST("Stop puts back the eye, lock and selection Play found when the game read another world in play") {
    Bench bench;
    Desk& desk = bench.desk;
    const std::string level = (openProject() / "level.json").string();
    Scene& scene = desk.scene();
    const EntityId crate = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    const EntityId ball  = createEntity(scene, desk.resources(), desk.state, EntityKind::Sphere);
    desk.state.selection.select(crate);
    desk.state.hiddenInView = {crate};
    REQUIRE(SceneSerializer::save(scene, desk.resources(), level));

    // The game reads a level: its ids are numbered afresh, and one is the ball's.
    REQUIRE(desk.startPlay());
    REQUIRE(SceneSerializer::load(scene, desk.resources(), level));
    const EntityId rock = scene.entityAt(ball.slot());
    REQUIRE(rock == ball);
    desk.state.selection.select(rock);
    desk.state.hiddenInView = {rock};
    desk.state.lockedInView = {rock};
    desk.stop();

    CHECK(desk.state.selection.entities() == std::vector<EntityId>{crate});
    CHECK(desk.state.hiddenInView == std::vector<EntityId>{crate});
    CHECK(desk.state.lockedInView.empty());
}

TEST("Stop keeps the eye, lock and selection set in play when the game read no other world") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    const EntityId crate = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    const EntityId ball  = createEntity(scene, desk.resources(), desk.state, EntityKind::Sphere);
    desk.state.selection.select(crate);
    desk.state.hiddenInView = {crate};

    REQUIRE(desk.startPlay());
    desk.state.selection.select(ball);
    desk.state.hiddenInView = {ball};
    desk.state.lockedInView = {ball};
    desk.stop();

    CHECK(desk.state.selection.entities() == std::vector<EntityId>{ball});
    CHECK(desk.state.hiddenInView == std::vector<EntityId>{ball});
    CHECK(desk.state.lockedInView == std::vector<EntityId>{ball});
}

TEST("a copy taken before Play pastes under its parent after Stop, though the game read another world") {
    Bench bench;
    Desk& desk = bench.desk;
    const std::string level = (openProject() / "level.json").string();
    Scene& scene = desk.scene();
    const EntityId holder = createEntity(scene, desk.resources(), desk.state, EntityKind::Empty);
    const EntityId cube   = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube, holder);
    REQUIRE(SceneSerializer::save(scene, desk.resources(), level));
    desk.state.selection.select(cube);
    copySelection(scene, desk.resources(), desk.state);

    REQUIRE(desk.startPlay());
    REQUIRE(SceneSerializer::load(scene, desk.resources(), level));
    desk.stop();
    paste(scene, desk.resources(), desk.state);
    const EntityId pasted = desk.state.selection.active();
    REQUIRE(pasted);
    CHECK(pasted != cube);
    const Hierarchy* link = scene.tryGet<Hierarchy>(pasted);
    REQUIRE(link);
    CHECK(link->parent == holder);
}

TEST("a copy taken in a world the game read in play pastes at the top level after Stop") {
    Bench bench;
    Desk& desk = bench.desk;
    const std::string level = (openProject() / "level.json").string();
    Scene& scene = desk.scene();
    const EntityId holder = createEntity(scene, desk.resources(), desk.state, EntityKind::Empty);
    const EntityId cube   = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube, holder);
    REQUIRE(SceneSerializer::save(scene, desk.resources(), level));

    // Taken under the level's holder, whose id is the authored holder's.
    REQUIRE(desk.startPlay());
    REQUIRE(SceneSerializer::load(scene, desk.resources(), level));
    REQUIRE(scene.get<Hierarchy>(scene.entityAt(cube.slot())).parent == holder);
    desk.state.selection.select(scene.entityAt(cube.slot()));
    copySelection(scene, desk.resources(), desk.state);
    desk.stop();
    paste(scene, desk.resources(), desk.state);
    const EntityId pasted = desk.state.selection.active();
    REQUIRE(pasted);
    const Hierarchy* link = scene.tryGet<Hierarchy>(pasted);
    CHECK(!(link && link->parent));
}

TEST("in play, undo reaches only what the session did, and Save is refused") {
    Bench bench;
    Desk& desk = bench.desk;
    const std::filesystem::path root = openProject();
    const std::string file = (root / "scenes" / "level.json").string();
    {
        Scene empty;
        ResourceManager none;
        REQUIRE(SceneSerializer::save(empty, none, file));
    }
    desk.session.loadPath(desk.ec.frame, desk.state, file);
    REQUIRE(desk.session.path() == file);
    const std::string onDisk = readText(file);

    const EntityId crate = createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Cube);
    desk.state.commands.endGesture();
    moveTo(desk, crate, {1.0f, 0.0f, 0.0f});

    REQUIRE(desk.startPlay());
    CHECK(!desk.state.commands.canUndo());
    moveTo(desk, crate, {2.0f, 0.0f, 0.0f});
    undo(desk.scene(), desk.state);
    undo(desk.scene(), desk.state);
    CHECK(desk.scene().get<Transform>(crate).position.x == 1.0f);

    desk.session.save(desk.ec.frame, desk.state);
    CHECK(desk.said("Stop the play session before saving"));
    CHECK(holds(file, onDisk));
    desk.session.requestSaveAs(desk.state);
    CHECK(!desk.session.isSaveDialogActive());

    desk.stop();
    CHECK(desk.state.commands.undoDepth() == 2);
    desk.session.save(desk.ec.frame, desk.state);
    CHECK(!holds(file, onDisk));
    CHECK(!desk.state.sceneDirty);
}

TEST("a session starts on the project's render settings and ends with them back") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Graphics& graphics = desk.ec.frame.graphics;
    RenderSettings project;
    project.renderScale = 0.75f;
    graphics.setProject(project);

    // A tier Render Settings previews is not the project's: Play ends it.
    desk.state.previewTier = GraphicsPreset::Low;
    graphics.setQuality(GraphicsPreset::Low);
    REQUIRE(graphics.settings().renderScale != 0.75f);
    REQUIRE(desk.startPlay());
    CHECK(!desk.state.previewTier);
    CHECK(graphics.settings().renderScale == 0.75f);

    // A game's settings screen picks a tier; Stop takes it back.
    graphics.setQuality(GraphicsPreset::Low);
    desk.stop();
    CHECK(graphics.settings().renderScale == 0.75f);
}

TEST("Play is refused while a terrain cannot be written, as Stop would lose its sculpting") {
    Bench bench;
    Desk& desk = bench.desk;
    const std::filesystem::path root = openProject();
    const EntityId ground = createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Terrain);
    REQUIRE(ground);
    // A file where the terrains' folder would go.
    writeText(root / "assets" / "terrain", "not a folder");

    CHECK(!desk.startPlay());
    CHECK(!desk.play.isPlaying());
    CHECK(desk.said("Play refused: a terrain could not be written"));
    // Nothing set aside: the authored history is still the one undo walks.
    CHECK(desk.state.commands.canUndo());
}

TEST("Stop leaves a terrain whose file will not read as it is, and the log says which") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    const EntityId ground    = createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Terrain);
    const TerrainHandle land = desk.scene().get<Terrain>(ground).terrain;
    REQUIRE(writeTerrainFiles(desk.resources()));
    const TerrainAsset& terrain = desk.resources().get(land);
    const std::string name = terrain.name();
    const std::filesystem::path file = Roots::resolveProjectPath(name);

    // Cut short two bytes into its body: still a terrain file, but a body that will not decode.
    const std::string bytes = readText(file);
    const size_t header     = bytes.size() - TerrainFile::encode(terrain, desk.resources()).size();
    writeText(file, bytes.substr(0, header + 2));
    const std::vector<float> heights = terrain.heights;
    readTerrainFiles(desk.resources());
    CHECK(desk.resources().get(land).heights == heights);
    CHECK(countInLog("Terrain '" + name + "' left as it is") == 1);
}

TEST("Stop leaves a terrain whose file is of another version as it is, and the log says which") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    const EntityId ground    = createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Terrain);
    const TerrainHandle land = desk.scene().get<Terrain>(ground).terrain;
    REQUIRE(writeTerrainFiles(desk.resources()));
    const std::string name = desk.resources().get(land).name();
    const std::filesystem::path file = Roots::resolveProjectPath(name);

    // The version after the magic, one this build does not read.
    std::string bytes = readText(file);
    REQUIRE(bytes.size() > sizeof(TerrainFile::MAGIC));
    bytes[sizeof(TerrainFile::MAGIC)] = static_cast<char>(TerrainFile::VERSION + 1);
    writeText(file, bytes);
    const std::vector<float> heights = desk.resources().get(land).heights;
    readTerrainFiles(desk.resources());
    CHECK(desk.resources().get(land).heights == heights);
    CHECK(countInLog("Terrain '" + name + "' left as it is") == 1);
}

TEST("Eject frees the cursor the game holds and Return gives it back as the game last set it") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    FrameContext& ctx     = desk.ec.frame;
    WindowManager& window = ctx.window;
    desk.play.setEjected(ctx, true);
    CHECK(!desk.play.isEjected());

    REQUIRE(desk.startPlay());
    window.setCursorMode(CursorMode::Disabled);
    desk.play.setEjected(ctx, true);
    CHECK(desk.play.isEjected());
    CHECK(window.cursorMode() == CursorMode::Normal);
    // The game, hearing nothing, takes the cursor again: it is held free, and what it asked kept.
    window.setCursorMode(CursorMode::Hidden);
    desk.play.holdEjectedCursorFree(ctx);
    CHECK(window.cursorMode() == CursorMode::Normal);
    desk.play.setEjected(ctx, false);
    CHECK(window.cursorMode() == CursorMode::Hidden);

    // Stop ends an ejection with the session.
    desk.play.setEjected(ctx, true);
    desk.stop();
    CHECK(!desk.play.isEjected());
    CHECK(window.cursorMode() == CursorMode::Normal);
}

TEST("Stop takes back the game's time scale and keeps the playbar's speed") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Clock& clock = desk.ec.frame.clock;
    clock.setHostScale(0.5f);
    REQUIRE(desk.startPlay());
    clock.setPaused(false);
    clock.setTimeScale(0.25f);
    CHECK(clock.getHostScale() == 0.5f);

    desk.stop();
    CHECK(clock.isPaused());
    CHECK(clock.getTimeScale() == 1.0f);
    CHECK(clock.getHostScale() == 0.5f);
}

TEST("the game ticks no more once Stop is pressed, though the editor stops it on its next frame") {
    Game game;
    EditorHost& editor = EditorHost::on(game);
    openProject();
    Pointer pointer;
    PlaybackBar bar;

    int atStop = -1;
    // Played and stopped by the key, the clock left to run on the frame's time as the editor's does.
    game.run(4, [&](int frame) {
        Desk& desk = editor.desk();
        if (frame == 1) {
            addBehavior<Ticks>(desk.scene(), desk.scene().createEntity());
            pressPlayStop(pointer, bar, desk);
            REQUIRE(desk.play.isPlaying());
        }
        if (frame == 3) {
            REQUIRE(Ticks::counted > 0);
            atStop = Ticks::counted;
            pressPlayStop(pointer, bar, desk);
        }
    }, 0);

    CHECK(!editor.desk().play.isPlaying());
    CHECK(Ticks::counted == atStop);
}

TEST("the game ticks no more once a new scene is asked in play, at once or by the prompt's Save") {
    // Its end clears the behaviors registered, so it outlives both games.
    ScriptModule module;
    for (const bool dirty : {false, true}) {
        Ticks::counted = 0;
        Game game;
        EditorHost& editor = EditorHost::on(game);
        openProject();
        Pointer pointer;
        PlaybackBar bar;

        // Asked on frame 3; a dirty scene's prompt is answered on frame 5, the game running meanwhile.
        const int answered = dirty ? 5 : 3;
        int atAsk    = -1;
        int atAnswer = -1;
        game.run(7, [&](int frame) {
            Desk& desk = editor.desk();
            // The editor's frame, as EditorSystem's: what the last one asked is carried out first.
            desk.state.sceneActions.resolve(desk.ec, module);
            if (frame == 1) {
                addBehavior<Ticks>(desk.scene(), desk.scene().createEntity());
                desk.state.sceneDirty = dirty;
                pressPlayStop(pointer, bar, desk);
                REQUIRE(desk.play.isPlaying());
            }
            if (frame == 3) {
                atAsk = Ticks::counted;
                desk.state.sceneActions.request(SceneActions::Kind::New);
            }
            if (frame == answered) atAnswer = Ticks::counted;
            const bool saving = dirty && frame == answered;
            if (saving) pointer.key(ImGuiKey_Enter, true);
            pointer.frame([&] { desk.state.sceneActions.drawDialog(desk.ec); });
            if (saving) pointer.key(ImGuiKey_Enter, false);
            desk.state.sceneActions.pauseForResolve(desk.ec);
        }, 0);

        CHECK(!editor.desk().play.isPlaying());
        CHECK(Ticks::counted == atAnswer);
        if (dirty) CHECK(atAnswer > atAsk);
    }
}

TEST("a project that will not open, asked in play, leaves the game playing, not paused") {
    ScriptModule module;
    Ticks::counted = 0;
    Game game;
    EditorHost& editor = EditorHost::on(game);
    openProject();
    Pointer pointer;
    PlaybackBar bar;

    int atAsk = -1;
    game.run(6, [&](int frame) {
        Desk& desk = editor.desk();
        desk.state.sceneActions.resolve(desk.ec, module);
        if (frame == 1) {
            addBehavior<Ticks>(desk.scene(), desk.scene().createEntity());
            pressPlayStop(pointer, bar, desk);
            REQUIRE(desk.play.isPlaying());
        }
        if (frame == 3) {
            atAsk = Ticks::counted;
            // A recent project since moved away, which the Recent Projects menu asks for unchecked.
            desk.state.sceneActions.request(SceneActions::Kind::OpenProject, (scratch() / "gone").string());
        }
        pointer.frame([&] { desk.state.sceneActions.drawDialog(desk.ec); });
        desk.state.sceneActions.pauseForResolve(desk.ec);
    }, 0);

    CHECK(editor.desk().play.isPlaying());
    CHECK(!editor.desk().ec.frame.clock.isPaused());
    CHECK(Ticks::counted > atAsk);
}
