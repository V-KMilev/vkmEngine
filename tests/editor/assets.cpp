// The asset browser's data paths with no window: files dropped on the editor brought in and imported,
// an asset given to an entity, renamed, and the engine's effects taken into a project.

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "actions/asset_actions.h"
#include "actions/asset_drop.h"
#include "actions/create_entity.h"
#include "actions/edit_actions.h"
#include "actions/prefab_actions.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/render/mesh.h"
#include "io/asset/asset_library.h"
#include "io/asset/generate/material_generators.h"
#include "io/project/roots.h"
#include "panels/asset_browser/asset_browser_panel.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/texture_asset.h"
#include "session/asset/file_import.h"

#include "editor/desk.h"
#include "harness/check.h"

using namespace Vkm::Test;
using namespace Vkm::Engine::EditorActions;

namespace {

namespace fs = std::filesystem;

/// One of the engine's own pictures and sounds, as files from elsewhere on the author's disk.
fs::path effect(
    const char* file
) { return engineDir() / "content" / "effects" / "assets" / "effects" / file; }

int countUnder(const fs::path& dir) {
    int files = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) files += entry.is_regular_file();
    return files;
}

} // namespace

TEST("a file dropped from outside the project is copied into its assets and imported, once") {
    Bench bench;
    Desk& desk = bench.desk;
    const fs::path root = openProject();
    const fs::path away = scratch() / "downloads";
    fs::create_directories(away);
    fs::copy_file(effect("smoke.png"), away / "smoke.png");
    fs::copy_file(effect("fire.wav"), away / "fire.wav");

    FileImport::importDropped(
        {(away / "smoke.png").string(), (away / "fire.wav").string()},
        desk.scene(),
        desk.resources(),
        desk.state
    );
    const fs::path picture = root / "assets" / "textures" / "smoke.png";
    const fs::path sound   = root / "assets" / "audio" / "fire.wav";
    CHECK(holds(picture, readText(away / "smoke.png")));
    CHECK(holds(sound, readText(away / "fire.wav")));
    CHECK(desk.resources().findByName<TextureAsset>("assets/textures/smoke.png"));
    CHECK(desk.resources().findByName<AudioClipAsset>("assets/audio/fire.wav"));
    CHECK(desk.state.sceneDirty);

    // The same bytes again are the file already there; other bytes of its name take a free one.
    FileImport::importDropped({(away / "smoke.png").string()}, desk.scene(), desk.resources(), desk.state);
    CHECK(countUnder(root / "assets" / "textures") == 1);
    fs::copy_file(effect("flames.png"), away / "smoke.png", fs::copy_options::overwrite_existing);
    FileImport::importDropped({(away / "smoke.png").string()}, desk.scene(), desk.resources(), desk.state);
    CHECK(holds(root / "assets" / "textures" / "smoke_2.png", readText(effect("flames.png"))));
    CHECK(holds(picture, readText(effect("smoke.png"))));
    CHECK(desk.resources().findByName<TextureAsset>("assets/textures/smoke_2.png"));

    // What it cannot read is refused by name, and leaves nothing behind.
    writeText(away / "notes.txt", "a list");
    FileImport::importDropped(
        {(away / "notes.txt").string(), away.string()},
        desk.scene(),
        desk.resources(),
        desk.state
    );
    CHECK(desk.said("notes.txt: not a model, an image or a sound"));
    CHECK(desk.said("is not a file"));
    CHECK(!fs::exists(root / "assets" / "notes.txt"));
}

TEST("a dropped model comes into a folder of its own with the files it names, placed as one step") {
    Bench bench;
    Desk& desk = bench.desk;
    const fs::path root = openProject();
    const fs::path away = scratch() / "downloads" / "kit";
    writeText(away / "tile.obj",
              "mtllib tile.mtl\nv 0 0 0\nv 1 0 0\nv 0 0 1\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 1 0\n"
              "usemtl stone\nf 1/1/1 3/3/1 2/2/1\n");
    writeText(away / "tile.mtl", "newmtl stone\nmap_Kd maps/stone.png\n");
    fs::create_directories(away / "maps");
    fs::copy_file(effect("smoke.png"), away / "maps" / "stone.png");

    FileImport::importDropped({(away / "tile.obj").string()}, desk.scene(), desk.resources(), desk.state);

    const fs::path folder = root / "assets" / "models" / "tile";
    CHECK(fs::exists(folder / "tile.obj"));
    CHECK(fs::exists(folder / "tile.mtl"));
    CHECK(holds(folder / "maps" / "stone.png", readText(effect("smoke.png"))));
    const EntityId placed = desk.state.selection.active();
    REQUIRE(placed);
    CHECK(desk.state.commands.undoDepth() == 1);
    undo(desk.scene(), desk.state);
    CHECK(!desk.scene().isAlive(placed));
}

TEST("an asset given to an entity is one step, and a pairing that does not fit is refused") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    ResourceManager& resources = desk.resources();
    const EntityId cube = createEntity(scene, resources, desk.state, EntityKind::Cube);
    desk.state.commands.endGesture();
    const MaterialHandle brick = copyMaterial(resources, generateDefaultMaterial(resources), "Brick");
    const MaterialHandle was   = scene.get<Mesh>(cube).material;
    FileImport::importDropped({effect("fire.wav").string()}, scene, resources, desk.state);
    const AudioClipHandle fire = resources.findByName<AudioClipAsset>("assets/audio/fire.wav");
    REQUIRE(fire);

    CHECK(assignsTo(AssetType::Material, scene, cube));
    assignAsset(scene, resources, desk.state, cube, AssetType::Material, brick.key);
    CHECK(scene.get<Mesh>(cube).material == brick);

    // A sound is given to an Audio Source, which a cube has none of; a texture to nothing. Every drop
    // target asks first, so the refused pairing is never handed on.
    const EntityId speaker = createEntity(scene, resources, desk.state, EntityKind::AudioSource);
    desk.state.commands.endGesture();
    CHECK(!assignsTo(AssetType::AudioClip, scene, cube));
    CHECK(!assignsTo(AssetType::Texture, scene, cube));
    CHECK(!assignsTo(AssetType::Material, scene, speaker));
    CHECK(assignsTo(AssetType::AudioClip, scene, speaker));
    assignAsset(scene, resources, desk.state, speaker, AssetType::AudioClip, fire.key);
    CHECK(scene.get<AudioSource>(speaker).clip == fire);

    CHECK(desk.state.commands.undoDepth() == 4);
    undo(scene, desk.state);
    CHECK(!scene.get<AudioSource>(speaker).clip);
    undo(scene, desk.state);
    undo(scene, desk.state);
    CHECK(scene.get<Mesh>(cube).material == was);
}

TEST("a renamed asset is named so by everything that uses it, a taken name is not stolen, and undo renames back") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    ResourceManager& resources = desk.resources();
    const EntityId cube = createEntity(scene, resources, desk.state, EntityKind::Cube);
    desk.state.commands.endGesture();
    const MaterialHandle brick = copyMaterial(resources, generateDefaultMaterial(resources), "Brick");
    const MaterialHandle moss  = copyMaterial(resources, generateDefaultMaterial(resources), "Moss");
    assignAsset(scene, resources, desk.state, cube, AssetType::Material, brick.key);
    desk.state.commands.endGesture();

    renameAsset(resources, desk.state, brick, "Brick", "Stone", "Rename Asset");
    desk.state.commands.endGesture();
    CHECK(resources.get(brick).name() == "Stone");
    CHECK(desk.sceneText().find("\"Stone\"") != std::string::npos);
    CHECK(desk.sceneText().find("\"Brick\"") == std::string::npos);

    renameAsset(resources, desk.state, moss, "Moss", "Stone", "Rename Asset");
    desk.state.commands.endGesture();
    const std::string given = resources.get(moss).name();
    CHECK(given != "Stone");
    CHECK(resources.get(brick).name() == "Stone");
    CHECK(desk.said("'Stone' was taken - renamed to '" + given + "'"));

    undo(scene, desk.state);
    CHECK(resources.get(moss).name() == "Moss");
    redo(scene, desk.state);
    CHECK(resources.get(moss).name() == given);
    CHECK(resources.get(brick).name() == "Stone");
    undo(scene, desk.state);
    undo(scene, desk.state);
    CHECK(resources.get(brick).name() == "Brick");
    CHECK(resources.findByName<MaterialAsset>("Brick") == brick);
    CHECK(desk.sceneText().find("\"Brick\"") != std::string::npos);
}

TEST("Create > Effect brings the engine's effects into the project once, and never over the project's own") {
    Bench bench;
    Desk& desk = bench.desk;
    const fs::path root = openProject();
    const std::vector<std::string> effects = shippedEffects();
    REQUIRE(!effects.empty());
    const int shipped = countUnder(engineDir() / "content" / "effects");

    const EntityId first = placeEffect(desk.scene(), desk.resources(), desk.state, effects.front());
    REQUIRE(first);
    CHECK(desk.scene().get<PrefabInstance>(first).source == "prefabs/effects/" + effects.front());
    CHECK(desk.said("Brought the engine's effects into the project (" + std::to_string(shipped) + " files)"));
    CHECK(countUnder(root / "prefabs" / "effects") == static_cast<int>(effects.size()));

    // The project's copy is its own: edited, it stays edited; a file deleted comes back.
    const fs::path ownCopy = root / "assets" / "effects" / "smoke.png";
    REQUIRE(fs::exists(ownCopy));
    fs::copy_file(effect("flames.png"), ownCopy, fs::copy_options::overwrite_existing);
    fs::remove(root / "assets" / "effects" / "fire.wav");

    const EntityId second = placeEffect(desk.scene(), desk.resources(), desk.state, effects.back());
    REQUIRE(second);
    CHECK(desk.said("(1 files)"));
    CHECK(holds(ownCopy, readText(effect("flames.png"))));
    CHECK(holds(root / "assets" / "effects" / "fire.wav", readText(effect("fire.wav"))));
    const std::vector<std::string> before = desk.sayings();
    CHECK(placeEffect(desk.scene(), desk.resources(), desk.state, effects.back()));
    CHECK(desk.sayings() == before);
}

TEST("a library asset that would not load is tried again by the next gesture, not each frame of one") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    // In the library, with neither a recipe nor a cooked file to load it from.
    AssetRecord missing;
    missing.type = AssetType::Material;
    missing.name = "test:missing";
    AssetLibrary::get().upsert(missing);
    Pointer pointer;
    AssetBrowserPanel panel;
    const auto draw = [&] { panel.draw(desk.ec); };

    // Inside its tile, where its menu is kept.
    const auto inTile = [](const auto& then) {
        ImGui::BeginChild("##abGrid");
        ImGui::PushID("test:missing");
        then();
        ImGui::PopID();
        ImGui::EndChild();
    };
    // The tile's menu, as a right click opens it, then its Load, as a click on it.
    const auto load = [&] {
        pointer.frame([&] {
            draw();
            inTile([] { ImGui::OpenPopupEx(ImGui::GetID("##tilectx")); });
        });
        ImGuiID item = 0;
        pointer.frame([&] {
            draw();
            inTile([&] {
                if (!ImGui::BeginPopup("##tilectx")) return;
                item = ImGui::GetID("Load");
                ImGui::EndPopup();
            });
        });
        ImGui::ActivateItemByID(item);
        pointer.frame(draw);
    };

    load();
    desk.state.toasts.push(ToastKind::Info, "between the two");
    load();
    const std::vector<std::string> said = desk.sayings();
    CHECK(std::count(said.begin(), said.end(), "Could not load test:missing") == 2);
}
