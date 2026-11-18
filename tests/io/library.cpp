// The asset library on disk: names that do not carry one machine's tree, a manifest that is derived
// data and refused whole when it is not this build's, and a terrain's own file.

#include <cstring>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "io/asset/asset_library.h"
#include "io/asset/recipe/material_recipe.h"
#include "io/asset/terrain_file.h"
#include "io/project/roots.h"
#include "io/scene/scene_assets.h"
#include "io/scene/scene_serializer.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;
using nlohmann::json;

TEST("a path under the project is named relative to it, so a project moved or cloned elsewhere still resolves") {
    const std::filesystem::path root = openProject("game");
    CHECK(Roots::toProjectRelative((root / "art" / "rock.glb").string()) == "art/rock.glb");
    CHECK(Roots::toProjectRelative((root / "art" / ".." / "art" / "rock.glb").string()) == "art/rock.glb");
    CHECK(Roots::toProjectRelative("art/rock.glb") == "art/rock.glb");
    // Outside the project it keeps its absolute path; a folder whose name starts with dots is inside.
    const std::filesystem::path outside = scratch() / "elsewhere" / "rock.glb";
    CHECK(Roots::toProjectRelative(outside.string()) == outside.generic_string());
    CHECK(Roots::toProjectRelative((root / "..cache" / "a.png").string()) == "..cache/a.png");

    // A name read back resolves against the open project, wherever it now is.
    CHECK(Roots::resolveProjectPath("art/rock.glb") == root / "art" / "rock.glb");
    const std::filesystem::path moved = openProject("moved");
    CHECK(Roots::resolveProjectPath("art/rock.glb") == moved / "art" / "rock.glb");
    CHECK(Roots::resolveProjectPath(outside.string()) == outside);
    CHECK(AssetLibrary::recipePath(AssetType::Material, "stone").string().rfind(moved.string(), 0) == 0);
}

TEST("a manifest of another version is refused whole, and every asset loads from its recipe") {
    openProject();
    ResourceManager authored;
    MaterialAsset stone;
    stone.albedo = {0.25f, 0.5f, 0.75f, 1.0f};
    const MaterialHandle made = authored.add(std::move(stone), "test:stone");
    REQUIRE(
        AssetLibrary::writeRecipe(
            AssetType::Material,
            "test:stone",
            materialToRecipe(authored.get(made), authored)
        )
    );
    AssetLibrary& library = AssetLibrary::get();
    AssetRecord cooked;
    cooked.type         = AssetType::Material;
    cooked.name         = "test:stone";
    cooked.recipeHash   = 42;
    cooked.documentHash = 7;
    library.upsert(cooked);
    REQUIRE(library.save());

    // A scene that names it, as the editor would have saved it.
    Scene scene;
    const EntityId block = scene.createEntity();
    Mesh shape;
    shape.material = made;
    scene.add(block, shape);
    const std::string saved = SceneSerializer::saveToString(scene, authored);

    const json written = json::parse(readText(Roots::assetManifest()));
    for (const int other : {-1, +1}) {
        json manifest = written;
        manifest["manifestVersion"] = written["manifestVersion"].get<int>() + other;
        writeText(Roots::assetManifest(), manifest.dump());
        const int before = countInLog("starting empty");

        library.load(AssetLibrary::Truth::Recipes);
        // Nothing of the row is kept: the recipe on disk is adopted afresh, with no cook recorded.
        const AssetRecord* row = library.find(AssetType::Material, "test:stone");
        REQUIRE(row != nullptr);
        CHECK(row->recipeHash == 0u);
        CHECK(countInLog("starting empty") == before + 1);

        ResourceManager fresh;
        Scene opened;
        REQUIRE(SceneSerializer::loadFromString(saved, opened, fresh));
        const MaterialHandle read = opened.get<Mesh>(block).material;
        REQUIRE(static_cast<bool>(read));
        CHECK(fresh.get(read).albedo == glm::vec4(0.25f, 0.5f, 0.75f, 1.0f));
    }
}

TEST("a terrain file of another version, or that is no terrain file, is refused, and this build's reads back whole") {
    openProject();
    ResourceManager resources;
    TerrainAsset ground;
    ground.resolution = TerrainAsset::MIN_RESOLUTION;
    ground.size       = 20.0f;
    ground.heights.assign(ground.resolution * ground.resolution, 0.0f);
    ground.weights.assign(ground.heights.size() * TerrainAsset::WEIGHTS_PER_VERTEX, 0);
    for (size_t i = 0; i < ground.heights.size(); ++i) ground.heights[i] = 0.01f * static_cast<float>(i % 97);
    ground.weights[5] = 200;
    const std::filesystem::path file = scratch() / "ground.terrain";
    REQUIRE(TerrainFile::write(file, ground, resources));

    std::vector<uint8_t> body;
    std::string error;
    REQUIRE(TerrainFile::readBody(file, body, error));
    TerrainAsset read;
    TerrainFile::Names names;
    REQUIRE(TerrainFile::decode(body.data(), body.size(), read, names, error));
    CHECK(read.resolution == ground.resolution);
    CHECK(read.size == ground.size);
    CHECK(read.heights == ground.heights);
    CHECK(read.weights == ground.weights);

    // The version follows the magic: any other is refused, newer or older.
    const std::string bytes = readText(file);
    for (const uint32_t version : {TerrainFile::VERSION + 1, TerrainFile::VERSION - 1}) {
        std::string other = bytes;
        std::memcpy(other.data() + sizeof(TerrainFile::MAGIC), &version, sizeof(version));
        writeText(file, other);
        error.clear();
        CHECK(!TerrainFile::readBody(file, body, error));
        CHECK(!error.empty());
    }
    std::string notTerrain = bytes;
    notTerrain[0] = 'X';
    writeText(file, notTerrain);
    CHECK(!TerrainFile::readBody(file, body, error));
    // Cut anywhere, a body is refused rather than read short.
    writeText(file, bytes);
    REQUIRE(TerrainFile::readBody(file, body, error));
    for (size_t cut = 0; cut < body.size(); cut += body.size() / 50 + 1) {
        TerrainAsset part;
        CHECK(!TerrainFile::decode(body.data(), cut, part, names, error));
    }
}

TEST("a material recipe's key no field of this build has is warned of, and the rest reads") {
    openProject();
    const json maps   = {{"albedo", ""}, {"glow", ""}};
    const json recipe = {{"roughness", 0.25}, {"water", "Body"}, {"textures", maps}};
    REQUIRE(AssetLibrary::writeRecipe(AssetType::Material, "old:pool", recipe));

    ResourceManager resources;
    REQUIRE(SceneAssets::loadNamed(AssetType::Material, "old:pool", resources));
    CHECK(countInLog("Material 'old:pool': its recipe's 'water'") == 1);
    CHECK(countInLog("Material 'old:pool': its recipe's 'textures.glow'") == 1);
    CHECK(countInLog("schema drift") == 2);
    CHECK(resources.get(resources.findByName<MaterialAsset>("old:pool")).roughness == 0.25f);
}
