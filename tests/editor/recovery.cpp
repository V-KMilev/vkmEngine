// Unsaved changes outlive a crash: the editor keeps a copy of a dirty scene in the user's folder, a
// save takes it away, and the scene opened again offers it back.

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "actions/create_entity.h"
#include "ecs/component/landscape/terrain.h"
#include "io/project/roots.h"
#include "io/scene/scene_serializer.h"
#include "session/scene_recovery.h"

#include "editor/desk.h"
#include "harness/check.h"

using namespace Vkm::Test;
using namespace Vkm::Engine::EditorActions;

namespace {

/// Date @p file's last save an hour back: a copy is offered when newer, and file times are coarse.
void savedAnHourAgo(const std::string& file) {
    std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) - std::chrono::hours(1));
}

/**
 * @brief The editor over a scene file of its own, opened as File > Open does.
 */
struct Editor : Bench {
    std::string file;

    explicit Editor(const std::filesystem::path& root) : file((root / "scenes" / "level.json").string()) {
        frame.clock.setPaused(true);
        frame.clock.beginFrame();
        if (!std::filesystem::exists(file)) {
            Scene empty;
            ResourceManager none;
            REQUIRE(SceneSerializer::save(empty, none, file));
            savedAnHourAgo(file);
        }
        desk.session.loadPath(desk.ec.frame, desk.state, file);
        REQUIRE(desk.session.path() == file);
    }

    /// One editor frame lasting @p seconds, in which the editor keeps its copy as every frame does.
    void frameOf(float seconds) {
        frame.clock.requestStep(static_cast<int>(seconds / frame.clock.getFixedStep() + 0.5f));
        frame.clock.beginFrame();
        while (frame.clock.consumeFixedStep()) {}
        desk.session.serviceRecovery(desk.ec.frame, desk.state);
    }

    std::filesystem::path copy() const {
        return SceneRecovery::pathFor(Roots::userRoot() / "recovery", Roots::projectRoot(), file);
    }
};

/// The file under @p dir and its folders, by path.
std::vector<std::filesystem::path> filesUnder(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file()) files.push_back(entry.path());
    }
    return files;
}

} // namespace

TEST("a scene with unsaved changes is copied to the user's folder a minute on, and a save takes the copy away") {
    const std::filesystem::path root = openProject();
    Editor editor(root);
    Desk& desk = editor.desk;
    const std::string saved = readText(editor.file);

    // Clean, nothing is kept however long it stays open.
    editor.frameOf(120.0f);
    CHECK(!std::filesystem::exists(editor.copy()));

    createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Cube);
    REQUIRE(desk.state.sceneDirty);
    editor.frameOf(59.0f);
    CHECK(!std::filesystem::exists(editor.copy()));
    editor.frameOf(1.0f);
    REQUIRE(std::filesystem::exists(editor.copy()));
    CHECK(editor.copy().string().rfind(Roots::userRoot().string(), 0) == 0);
    CHECK(SceneRecovery::hasNewer(editor.copy(), editor.file));
    CHECK(holds(editor.file, saved));

    // The copy is the scene as it stands, unsaved cube and all.
    Scene kept;
    ResourceManager keptAssets;
    REQUIRE(SceneSerializer::load(kept, keptAssets, editor.copy().string()));
    int entities = 0;
    kept.forEachEntity([&](EntityId) { ++entities; });
    CHECK(entities == 1);

    desk.session.save(desk.ec.frame, desk.state);
    CHECK(!desk.state.sceneDirty);
    CHECK(!std::filesystem::exists(editor.copy()));
    CHECK(!SceneRecovery::hasNewer(editor.copy(), editor.file));
}

TEST("a crash's copy is offered when its scene opens again, and Recover brings the changes back unsaved") {
    const std::filesystem::path root = openProject();
    std::vector<float> dug;
    std::string        terrainFile;
    {
        Editor before(root);
        Desk& desk = before.desk;
        const EntityId ground = createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Terrain);
        desk.session.save(desk.ec.frame, desk.state);
        savedAnHourAgo(before.file);
        const TerrainHandle land = desk.scene().get<Terrain>(ground).terrain;
        terrainFile = readText(Roots::resolveProjectPath(desk.resources().get(land).name()));
        REQUIRE(!terrainFile.empty());

        // Unsaved: a stroke on the ground, and a cube.
        desk.resources().edit(land).heights[0] = 3.0f;
        desk.resources().commit(land);
        createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Cube);
        dug = desk.resources().get(land).heights;
        before.frameOf(60.0f);
        REQUIRE(std::filesystem::exists(before.copy()));
        // The editor goes down here, with no clean close to take the copy away.
    }
    // Nothing the project holds was rewritten.
    CHECK(filesUnder(root / "scenes").size() == 1);
    const auto terrains = filesUnder(root / "assets" / "terrain");
    REQUIRE(terrains.size() == 1);
    CHECK(holds(terrains[0], terrainFile));

    Editor after(root);
    Desk& desk = after.desk;
    int opened = 0;
    desk.scene().forEachEntity([&](EntityId) { ++opened; });
    CHECK(opened == 1);

    // Recover is asked at the prompt and done before the next frame's ImGui, as it opens a world.
    Pointer pointer;
    pointer.frame([&] { desk.session.drawDialogs(desk.ec.frame, desk.state); });
    pointer.key(ImGuiKey_Enter, true);
    pointer.frame([&] { desk.session.drawDialogs(desk.ec.frame, desk.state); });
    desk.session.serviceRecovery(desk.ec.frame, desk.state);

    int recovered = 0;
    TerrainHandle land;
    desk.scene().forEachEntity([&](EntityId) { ++recovered; });
    desk.scene().forEach<Terrain>([&](EntityId, const Terrain& terrain) { land = terrain.terrain; });
    CHECK(recovered == 2);
    REQUIRE(desk.resources().isAlive(land));
    CHECK(desk.resources().get(land).heights == dug);
    CHECK(desk.state.sceneDirty);
    CHECK(desk.said("Recovered the unsaved changes"));
}

TEST("a copy not taken back is kept until the scene is saved, and no newer copy replaces it") {
    const std::filesystem::path root = openProject();
    {
        Editor before(root);
        createEntity(before.desk.scene(), before.desk.resources(), before.desk.state, EntityKind::Cube);
        before.frameOf(60.0f);
    }
    Editor after(root);
    Desk& desk = after.desk;
    const std::string kept = readText(after.copy());
    REQUIRE(!kept.empty());

    // Cancel: the copy stays, and an hour of new edits writes no other over it.
    Pointer pointer;
    pointer.frame([&] { desk.session.drawDialogs(desk.ec.frame, desk.state); });
    pointer.key(ImGuiKey_Escape, true);
    pointer.frame([&] { desk.session.drawDialogs(desk.ec.frame, desk.state); });
    createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Sphere);
    after.frameOf(3600.0f);
    CHECK(holds(after.copy(), kept));

    desk.session.save(desk.ec.frame, desk.state);
    CHECK(!std::filesystem::exists(after.copy()));
}
