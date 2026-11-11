// The scene's files: an open is a clean break that a bad file cannot make and a copy pastes across,
// what would throw unsaved changes away asks first, Save As names the file, and a new build of the
// module is reloaded.

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "actions/component_actions.h"
#include "actions/create_entity.h"
#include "actions/edit_actions.h"
#include "actions/entity_clipboard.h"
#include "core/graphics.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/hierarchy_operations.h"
#include "io/project/roots.h"
#include "io/scene/scene_serializer.h"
#include "resource/asset/audio_clip_asset.h"
#include "session/asset/file_import.h"
#include "session/project/reload_watcher.h"
#include "session/scene_actions.h"
#include "system/script/module/script_module.h"

#include "editor/desk.h"
#include "harness/check.h"

using namespace Vkm::Test;
using namespace Vkm::Engine::EditorActions;

namespace {

namespace fs = std::filesystem;

int countEntities(const Scene& scene) {
    int count = 0;
    scene.forEachEntity([&](EntityId) { ++count; });
    return count;
}

/// A scene file under the project's scenes/ holding @p cubes cubes, saved as the editor saves one.
std::string sceneFile(const fs::path& root, const char* name, int cubes) {
    Bench maker;
    Desk& desk = maker.desk;
    for (int i = 0; i < cubes; ++i) createEntity(
        desk.scene(),
        desk.resources(),
        desk.state,
        EntityKind::Cube
    );
    const std::string file = (root / "scenes" / name).string();
    desk.session.adoptPath(desk.scene(), desk.state, file);
    desk.session.save(desk.ec.frame, desk.state);
    REQUIRE(!desk.state.sceneDirty);
    return file;
}

} // namespace

TEST("a scene that will not load leaves the open one as it was: its world, its path and its history") {
    const fs::path root     = openProject();
    const std::string level = sceneFile(root, "level.json", 1);
    Bench bench;
    Desk& desk = bench.desk;
    desk.session.loadPath(desk.ec.frame, desk.state, level);
    const EntityId added = createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Sphere);
    desk.state.selection.select(added);

    writeText(root / "scenes" / "broken.json", "{ \"version\": ");
    writeText(root / "scenes" / "future.json", "{ \"version\": 999, \"entities\": [] }");
    for (const char* bad : {"broken.json", "future.json", "missing.json"}) {
        const std::string file = (root / "scenes" / bad).string();
        desk.session.loadPath(desk.ec.frame, desk.state, file);
        CHECK(desk.said("Load failed: " + file));
        CHECK(desk.session.path() == level);
        CHECK(desk.scene().isAlive(added));
        CHECK(countEntities(desk.scene()) == 2);
        CHECK(desk.state.commands.undoDepth() == 1);
        CHECK(desk.state.selection.active() == added);
        CHECK(desk.state.sceneDirty);
    }
}

TEST("an open is a clean break: history, selection, eye and lock go, and an import no scene names stays behind") {
    const fs::path root     = openProject();
    const std::string level = sceneFile(root, "level.json", 1);
    const std::string other = sceneFile(root, "other.json", 3);
    Bench bench;
    Desk& desk = bench.desk;
    desk.session.loadPath(desk.ec.frame, desk.state, level);
    const EntityId added = createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Sphere);
    desk.state.selection.select(added);
    desk.state.hiddenInView.push_back(added);
    desk.state.lockedInView.push_back(added);
    // A sound brought in and given to nothing yet.
    FileImport::importDropped(
        {(engineDir() / "content" / "effects" / "assets" / "effects" / "fire.wav").string()},
        desk.scene(),
        desk.resources(),
        desk.state
    );
    REQUIRE(desk.resources().findByName<AudioClipAsset>("assets/audio/fire.wav"));

    desk.session.loadPath(desk.ec.frame, desk.state, other);
    CHECK(desk.session.path() == other);
    CHECK(countEntities(desk.scene()) == 3);
    CHECK(!desk.state.commands.canUndo());
    CHECK(desk.state.selection.entities().empty());
    CHECK(desk.state.hiddenInView.empty());
    CHECK(desk.state.lockedInView.empty());
    CHECK(!desk.state.sceneDirty);
    CHECK(!desk.resources().findByName<AudioClipAsset>("assets/audio/fire.wav"));
    CHECK(desk.said("1 unused import(s) stayed with the previous scene"));
    CHECK(desk.state.recentScenes.front() == other);
}

TEST("a copy pastes under its parent in its own scene, and in another at the root where it stood") {
    const fs::path root     = openProject();
    const std::string level = sceneFile(root, "level.json", 0);
    const std::string other = sceneFile(root, "other.json", 3);
    Bench bench;
    Desk& desk   = bench.desk;
    Scene& scene = desk.scene();
    desk.session.loadPath(desk.ec.frame, desk.state, level);
    const EntityId holder = createEntity(scene, desk.resources(), desk.state, EntityKind::Empty);
    scene.get<Transform>(holder).position = {10.0f, 0.0f, 0.0f};
    const EntityId cube = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube, holder);
    const auto worldAt  = [&](EntityId id) { return glm::vec3(
        HierarchyOperations::computeWorldMatrix(scene, id)[3]
    ); };

    desk.state.selection.select(cube);
    copySelection(scene, desk.resources(), desk.state);
    paste(scene, desk.resources(), desk.state);
    const EntityId here = desk.state.selection.active();
    REQUIRE(here);
    CHECK(here != cube);
    CHECK(scene.get<Hierarchy>(here).parent == holder);
    CHECK_NEAR(worldAt(here), glm::vec3(10.0f, 0.0f, 0.0f), 1e-5f);

    // Another scene: its entities hold the slots the copy's parent had, and none of them is it.
    desk.session.loadPath(desk.ec.frame, desk.state, other);
    paste(scene, desk.resources(), desk.state);
    const EntityId there = desk.state.selection.active();
    REQUIRE(there);
    const Hierarchy* link = scene.tryGet<Hierarchy>(there);
    CHECK(!(link && link->parent));
    CHECK_NEAR(worldAt(there), glm::vec3(10.0f, 0.0f, 0.0f), 1e-5f);
    CHECK(desk.resources().isAlive(scene.get<Mesh>(there).mesh));
    CHECK(desk.state.commands.undoDepth() == 1);
}

TEST("what would throw a dirty scene away asks first: Save writes it before going on, Cancel keeps it") {
    const fs::path root     = openProject();
    const std::string level = sceneFile(root, "level.json", 1);
    const std::string other = sceneFile(root, "other.json", 3);
    Bench bench;
    Desk& desk = bench.desk;
    ScriptModule module;
    desk.session.loadPath(desk.ec.frame, desk.state, level);
    createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Sphere);
    const std::string saved = readText(level);
    Pointer pointer;
    const auto frame = [&] {
        desk.state.sceneActions.resolve(desk.ec, module);
        pointer.frame([&] { desk.state.sceneActions.drawDialog(desk.ec); });
    };

    // Asked: nothing happens until it is answered, and a second ask while it waits is dropped.
    desk.state.sceneActions.request(SceneActions::Kind::Open, other);
    desk.state.sceneActions.request(SceneActions::Kind::New);
    frame();
    frame();
    CHECK(desk.state.sceneActions.pending() == SceneActions::Kind::Open);
    CHECK(desk.session.path() == level);
    CHECK(countEntities(desk.scene()) == 2);

    pointer.key(ImGuiKey_Escape, true);
    frame();
    pointer.key(ImGuiKey_Escape, false);
    frame();
    CHECK(desk.state.sceneActions.pending() == SceneActions::Kind::None);
    CHECK(desk.session.path() == level);
    CHECK(countEntities(desk.scene()) == 2);
    CHECK(holds(level, saved));

    desk.state.sceneActions.request(SceneActions::Kind::Open, other);
    frame();
    pointer.key(ImGuiKey_Enter, true);
    frame();
    pointer.key(ImGuiKey_Enter, false);
    frame();
    CHECK(desk.session.path() == other);
    CHECK(countEntities(desk.scene()) == 3);
    Scene written;
    ResourceManager writtenAssets;
    REQUIRE(SceneSerializer::load(written, writtenAssets, level));
    CHECK(countEntities(written) == 2);

    // A clean scene goes at once.
    desk.state.sceneActions.request(SceneActions::Kind::New);
    frame();
    CHECK(desk.state.sceneActions.pending() == SceneActions::Kind::None);
    CHECK(!desk.session.hasPath());
}

TEST("Save As names the scene's file, and the first scene saved is where the game starts") {
    const fs::path root = openProject();
    writeText(root / "project.json", "{ \"name\": \"Test\" }");
    Bench bench;
    Desk& desk = bench.desk;
    createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Cube);
    REQUIRE(!desk.session.hasPath());

    desk.session.save(desk.ec.frame, desk.state);
    CHECK(desk.session.isSaveDialogActive());
    Pointer pointer;
    const auto frame = [&] { pointer.frame([&] { desk.session.drawDialogs(desk.ec.frame, desk.state); }); };
    frame();
    pointer.key(ImGuiKey_Enter, true);
    frame();
    pointer.key(ImGuiKey_Enter, false);
    frame();

    const std::string file = (root / "scenes" / "scene.json").string();
    CHECK(desk.session.path() == file);
    CHECK(!desk.state.sceneDirty);
    Scene written;
    ResourceManager writtenAssets;
    REQUIRE(SceneSerializer::load(written, writtenAssets, file));
    CHECK(countEntities(written) == 1);
    CHECK(desk.state.project.entryScene == "scenes/scene.json");
    CHECK(nlohmann::json::parse(
        readText(root / "project.json")
    ).value("entryScene", "") == "scenes/scene.json");
    CHECK(desk.said("The game now starts in scenes/scene.json"));
}

TEST("a new build of the module, seen by its stamp, is reloaded, and a stamp already there at the open is no build") {
    openProject();
    Bench bench;
    Desk& desk = bench.desk;
    createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Cube);
    ScriptModule module;
    module.expect(Roots::gameplayModule(), desk.ec.frame.events);
    const fs::path stamp = ReloadWatcher::stampOf(Roots::gameplayModule());
    writeText(stamp, "");
    const fs::file_time_type built = fs::last_write_time(stamp);
    ReloadWatcher watcher(module, bench.behaviors);

    // A reload takes the world through its snapshot and back: a new epoch is a reload that ran.
    const auto reloads = [&] {
        const uint64_t epoch = desk.scene().epoch();
        watcher.update(desk.ec);
        return desk.scene().epoch() != epoch;
    };
    CHECK(!reloads());
    CHECK(!reloads());
    CHECK(desk.sayings().empty());

    // The build lands: its stamp moves. There is no module to load here, so the reload says it failed.
    fs::last_write_time(stamp, built + std::chrono::seconds(10));
    CHECK(reloads());
    CHECK(desk.said("Script reload failed"));
    CHECK(!reloads());

    // A stamp gone and back is a build too; File > Reload Scripts asks without one.
    fs::remove(stamp);
    CHECK(!reloads());
    writeText(stamp, "");
    CHECK(reloads());
    desk.state.requests.ask(Ask::ReloadScripts);
    CHECK(reloads());
    CHECK(countEntities(desk.scene()) == 1);
}

TEST("the Hierarchy's eye and lock hold over Stop and over a script reload, which bring the world back") {
    openProject();
    Bench bench;
    Desk&  desk  = bench.desk;
    Scene& scene = desk.scene();
    // Made in slots used before, so each id is past its slot's first generation.
    const EntityId first  = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    const EntityId second = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    deleteEntity(scene, desk.state, first);
    deleteEntity(scene, desk.state, second);
    const EntityId hidden = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    const EntityId locked = createEntity(scene, desk.resources(), desk.state, EntityKind::Sphere);
    desk.state.hiddenInView = {hidden};
    desk.state.lockedInView = {locked};
    ScriptModule module;
    ReloadWatcher watcher(module, bench.behaviors);

    // Each names its entity in the world that came back, under the id it had.
    const auto names = [&](const std::vector<EntityId>& list, EntityId was) {
        return list.size() == 1 && scene.isAlive(list[0]) && list[0] == was;
    };
    REQUIRE(desk.startPlay());
    desk.stop();
    CHECK(names(desk.state.hiddenInView, hidden));
    CHECK(names(desk.state.lockedInView, locked));
    const uint64_t epoch = scene.epoch();
    desk.state.requests.ask(Ask::ReloadScripts);
    watcher.update(desk.ec);
    REQUIRE(scene.epoch() != epoch);
    CHECK(names(desk.state.hiddenInView, hidden));
    CHECK(names(desk.state.lockedInView, locked));
}

TEST("a script reload in Edit mode brings every entity back under the id it had") {
    openProject();
    Bench bench;
    Desk&  desk  = bench.desk;
    Scene& scene = desk.scene();
    // In a slot used before, so its id is past the slot's first generation.
    deleteEntity(scene, desk.state, createEntity(scene, desk.resources(), desk.state, EntityKind::Cube));
    const EntityId crate = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    createEntity(scene, desk.resources(), desk.state, EntityKind::Sphere);
    REQUIRE(crate.generation() > 0);
    const std::vector<EntityId> before = entityIds(scene);
    ScriptModule module;
    ReloadWatcher watcher(module, bench.behaviors);

    const uint64_t epoch = scene.epoch();
    desk.state.requests.ask(Ask::ReloadScripts);
    watcher.update(desk.ec);
    REQUIRE(scene.epoch() != epoch);
    CHECK(entityIds(scene) == before);
}

TEST("a deleted entity undone after a script reload in Edit mode is named again by what held it") {
    openProject();
    Bench bench;
    Desk& desk = bench.desk;
    Scene& scene = desk.scene();
    const EntityId door  = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    const EntityId frame = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    Joint hinge;
    hinge.connected = door;
    addComponent(scene, desk.state, frame, hinge, "Joint", "Add Joint");
    deleteEntity(scene, desk.state, door);
    desk.state.commands.endGesture();
    ScriptModule module;
    ReloadWatcher watcher(module, bench.behaviors);

    const uint64_t epoch = scene.epoch();
    desk.state.requests.ask(Ask::ReloadScripts);
    watcher.update(desk.ec);
    REQUIRE(scene.epoch() != epoch);
    undo(scene, desk.state);
    const EntityId back = scene.get<Joint>(frame).connected;
    CHECK(scene.isAlive(back));
    CHECK(back.slot() == door.slot());
}

TEST("the eye and lock let go at Stop of an entity play made, whose id the next one may take") {
    openProject();
    Bench bench;
    Desk&  desk  = bench.desk;
    Scene& scene = desk.scene();
    // A slot free at Play, which hands out after Stop the id play took in it.
    deleteEntity(scene, desk.state, createEntity(scene, desk.resources(), desk.state, EntityKind::Cube));
    REQUIRE(desk.startPlay());
    const EntityId made = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    desk.state.hiddenInView = {made};
    desk.state.lockedInView = {made};
    desk.stop();

    REQUIRE(createEntity(scene, desk.resources(), desk.state, EntityKind::Cube) == made);
    CHECK(desk.state.hiddenInView.empty());
    CHECK(desk.state.lockedInView.empty());
}

TEST("a copy taken in play under an entity play made pastes at the top level after Stop") {
    openProject();
    Bench bench;
    Desk&  desk  = bench.desk;
    Scene& scene = desk.scene();
    // A slot free at Play, which hands out after Stop the id play took in it.
    deleteEntity(scene, desk.state, createEntity(scene, desk.resources(), desk.state, EntityKind::Cube));
    REQUIRE(desk.startPlay());
    const EntityId holder = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    const EntityId held   = createEntity(scene, desk.resources(), desk.state, EntityKind::Sphere);
    HierarchyOperations::setParent(scene, held, holder);
    desk.state.selection.select(held);
    copySelection(scene, desk.resources(), desk.state);
    desk.stop();

    REQUIRE(createEntity(scene, desk.resources(), desk.state, EntityKind::Cube) == holder);
    paste(scene, desk.resources(), desk.state);
    const EntityId pasted = desk.state.selection.active();
    REQUIRE(scene.isAlive(pasted));
    const Hierarchy* node = scene.tryGet<Hierarchy>(pasted);
    const bool atRoot     = !node || !node->parent;
    CHECK(atRoot);
}

TEST("a scene that will not load in a play session stops the session, whose behaviors the open ended") {
    const fs::path root     = openProject();
    const std::string level = sceneFile(root, "level.json", 1);
    Bench bench;
    Desk& desk = bench.desk;
    desk.session.loadPath(desk.ec.frame, desk.state, level);
    REQUIRE(desk.startPlay());

    const std::string broken = (root / "scenes" / "broken.json").string();
    writeText(broken, "{ \"version\": ");
    desk.session.loadPath(desk.ec.frame, desk.state, broken);
    CHECK(desk.said("Load failed: " + broken));
    CHECK(!desk.play.isPlaying());
    CHECK(desk.session.path() == level);
    CHECK(countEntities(desk.scene()) == 1);
}

TEST("a script reload in Edit mode keeps the tier Render Settings shows, as its panel still says it does") {
    openProject();
    Bench bench;
    Desk& desk = bench.desk;
    Graphics& graphics = desk.ec.frame.graphics;
    RenderSettings project;
    project.renderScale = 0.75f;
    graphics.setProject(project);
    desk.state.previewTier = GraphicsPreset::Low;
    graphics.setQuality(GraphicsPreset::Low);
    const float previewed = graphics.settings().renderScale;
    REQUIRE(previewed != 0.75f);

    ScriptModule module;
    ReloadWatcher watcher(module, bench.behaviors);
    const uint64_t epoch = desk.scene().epoch();
    desk.state.requests.ask(Ask::ReloadScripts);
    watcher.update(desk.ec);
    REQUIRE(desk.scene().epoch() != epoch);
    CHECK(desk.state.previewTier == GraphicsPreset::Low);
    CHECK(graphics.settings().renderScale == previewed);
}
