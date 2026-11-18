// The cook as the editor's save and vkm_cook run it: every asset recorded and baked into the
// project's cache, which a host then reads instead of the source art, baked again only when what it
// was made from changed.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include "base/hash/fnv1a.h"
#include "cook/asset_cooker.h"
#include "cook/mesh_gpu_order.h"
#include "cook/texture/texture_bake.h"
#include "import/audio_import.h"
#include "import/model/model_import.h"
#include "import/model/source_model.h"
#include "import/recipe_registration.h"
#include "import/texture_import.h"
#include "io/asset/asset_library.h"
#include "io/asset/cooked/asset_cook.h"
#include "io/asset/generate/mesh_generators.h"
#include "io/asset/recipe/asset_factory.h"
#include "io/project/roots.h"
#include "io/scene/scene_assets.h"
#include "mesh/lightmap_unwrap.h"
#include "mesh/lod_generator.h"
#include "mesh/mesh_decimate.h"
#include "resource/resource_manager.h"
#include "system/async_loader/async_loader_system.h"

#include "harness/check.h"
#include "harness/world.h"
#include "pipeline/art.h"

using namespace Vkm::Audio;
using namespace Vkm::Test;
namespace fs = std::filesystem;

namespace {

/// Eight by eight texels of color, alpha and all, @p shift telling two exports apart.
std::vector<uint8_t> wallTexels(int shift) {
    std::vector<uint8_t> out;
    for (int i = 0; i < 64; ++i) {
        out.insert(out.end(), {uint8_t(i * 4 + shift), uint8_t(255 - i * 3), uint8_t(i * 7 % 256), 255});
    }
    return out;
}

std::vector<int16_t> chimeSamples() {
    std::vector<int16_t> out(2 * 200);
    for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<int16_t>(i * 151 % 30000 - 15000);
    return out;
}

/**
 * @brief One asset of each kind a project's art makes, as the editor imports it, in a project of its own.
 */
struct Project {
    struct Item {
        AssetType   type;
        std::string name;
    };

    fs::path          root;
    ResourceManager   resources;
    std::vector<Item> items;

    Project() {
        root = openProject();
        registerRecipeAssetFactories();
        AssetLibrary::get().load(AssetLibrary::Truth::Recipes);
        writeImage(root / "assets/wall.png", 8, 8, 4, wallTexels(0));
        writeWav(root / "assets/chime.wav", 22050, 2, chimeSamples());
        writeText(root / "assets/rig.gltf", riggedGltf());

        MaterialAsset wall;
        wall.albedoTexture = loadTexture("assets/wall.png", resources, TextureUsage::Color);
        wall.roughness     = 0.25f;
        resources.add(std::move(wall), "wall");
        const MeshHandle cube = addGeneratedMesh(resources, generateCube());
        loadAudioClip("assets/chime.wav", resources);
        loadModelSkeleton("assets/rig.gltf", resources);
        loadModelAnimationClip("assets/rig.gltf", 0, {{"step", 0.5f}}, resources);
        requestModelMeshAsync("assets/rig.gltf", 0, resources);
        REQUIRE(awaitAsyncLoads(resources));

        items = {
            {AssetType::Texture, "assets/wall.png"},
            {AssetType::Material, "wall"},
            {AssetType::Mesh, resources.get(cube).name()},
            {AssetType::Mesh, "assets/rig.gltf:mesh0"},
            {AssetType::AudioClip, "assets/chime.wav"},
            {AssetType::Skeleton, "assets/rig.gltf:skeleton"},
            {AssetType::AnimationClip, "assets/rig.gltf:clip0"},
        };
    }

    /// Where (@p type, @p name)'s cooked file is, by its row.
    static fs::path cookedOf(AssetType type, const std::string& name) {
        const AssetRecord* record = AssetLibrary::get().find(type, name);
        REQUIRE(record);
        return AssetLibrary::cookedPath(type, name, record->recipeHash);
    }
};

/// Every file under the project's library and cooked cache but the manifest, its write time set
/// back an hour: a file written again shows a time that is not this.
std::map<fs::path, fs::file_time_type> markFiles(const fs::path& root) {
    std::map<fs::path, fs::file_time_type> marks;
    for (const fs::path& dir : {root / "library", root / "cooked"}) {
        for (const auto& entry : fs::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file() || entry.path().filename() == "_manifest.json") continue;
            const fs::file_time_type back = fs::last_write_time(entry.path()) - std::chrono::hours(1);
            fs::last_write_time(entry.path(), back);
            marks[entry.path()] = back;
        }
    }
    return marks;
}

/// The marked files no longer as they were marked: rewritten or gone.
std::vector<std::string> touched(const std::map<fs::path, fs::file_time_type>& marks) {
    std::vector<std::string> out;
    for (const auto& [path, mark] : marks) {
        std::error_code ec;
        const bool rewritten = !fs::exists(path, ec) || fs::last_write_time(path) != mark;
        if (rewritten) out.push_back(path.filename().string());
    }
    return out;
}

size_t filesUnder(const fs::path& dir) {
    size_t count = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) count += entry.is_regular_file();
    return count;
}

} // namespace

TEST("a cook records every asset and writes each its cooked file, and a second cook rewrites none") {
    Project project;
    REQUIRE(AssetCooker::cookAllAssets(project.resources));
    std::error_code ec;
    CHECK(fs::exists(Roots::assetManifest(), ec));
    for (const auto& [type, name] : project.items) {
        std::printf("  %s\n", name.c_str());
        const AssetRecord* record = AssetLibrary::get().find(type, name);
        REQUIRE(record);
        CHECK(fs::exists(AssetLibrary::recipePath(type, name), ec));
        const fs::path cooked = Project::cookedOf(type, name);
        // A material's recipe is its runtime form: it has no cooked file.
        CHECK(fs::exists(cooked, ec) == (type != AssetType::Material));
        CHECK(AssetCook::isCookedCurrent(type, cooked) == (type != AssetType::Material));
    }
    // The source art each read is recorded, for a package to leave behind.
    CHECK(AssetLibrary::get().find(AssetType::Texture, "assets/wall.png")->sources
          == std::vector<std::string>{"assets/wall.png"});

    const size_t files = filesUnder(project.root / "cooked");
    const auto   marks = markFiles(project.root);
    REQUIRE(AssetCooker::cookAllAssets(project.resources));
    CHECK(touched(marks).empty());
    CHECK(filesUnder(project.root / "cooked") == files);
}

TEST("what a cook wrote loads back in a fresh graph from the cooked files, as the assets they were") {
    Project project;
    REQUIRE(AssetCooker::cookAllAssets(project.resources));
    const ResourceManager& made = project.resources;

    ResourceManager loaded;
    for (const auto& [type, name] : project.items) CHECK(SceneAssets::loadNamed(type, name, loaded));
    REQUIRE(awaitAsyncLoads(loaded));

    // The texture as the cook baked it: its whole chain, in blocks.
    const TextureAsset& wall = loaded.get(loaded.findByName<TextureAsset>("assets/wall.png"));
    CHECK(wall.params.internalFormat == TextureInternalFormat::BC7SRGBA);
    CHECK(wall.params.mipLevels == 4);
    CHECK(wall.sourceJson().value("kind", std::string()) == "cooked");
    const MaterialAsset& material = loaded.get(loaded.findByName<MaterialAsset>("wall"));
    CHECK(material.albedoTexture == loaded.findByName<TextureAsset>("assets/wall.png"));
    CHECK(material.roughness == 0.25f);

    for (const std::string& name : {project.items[2].name, project.items[3].name}) {
        const MeshHandle there = made.findByName<MeshAsset>(name);
        const MeshHandle here  = loaded.findByName<MeshAsset>(name);
        REQUIRE(there);
        REQUIRE(here);
        const MeshAsset& a = made.get(there);
        const MeshAsset& b = loaded.get(here);
        CHECK(b.vertices.size() == a.vertices.size());
        CHECK(b.indices.size() == a.indices.size());
        CHECK(b.boundsMin == a.boundsMin);
        CHECK(b.boundsMax == a.boundsMax);
        CHECK(b.skin.size() == a.skin.size());
        CHECK(b.skeleton == a.skeleton);
        CHECK(b.lightmapUVs.size() == a.lightmapUVs.size());
        CHECK(b.sourceJson().value("kind", std::string()) == "cooked");
    }
    const MeshHandle cube = loaded.findByName<MeshAsset>(project.items[2].name);
    REQUIRE(cube);
    CHECK(loaded.get(cube).lightmapUVs.size() == loaded.get(cube).vertices.size());

    const AudioClipAsset& chime = loaded.get(loaded.findByName<AudioClipAsset>("assets/chime.wav"));
    CHECK(*chime.samples == chimeSamples());
    CHECK(chime.sampleRate == 22050);
    const SkeletonAsset& rig = loaded.get(loaded.findByName<SkeletonAsset>("assets/rig.gltf:skeleton"));
    CHECK(rig.bones.size() == 2);
    const AnimationClipHandle sways = loaded.findByName<AnimationClipAsset>("assets/rig.gltf:clip0");
    const AnimationClipAsset& sway  = loaded.get(sways);
    CHECK(sway.duration == 1.0f);
    REQUIRE(sway.markers.size() == 1);
    CHECK(sway.markers[0].name == "step");
}

TEST("a host with no importer reads a package's cooked files and names what is missing") {
    Project project;
    REQUIRE(AssetCooker::cookAllAssets(project.resources));
    // What a package ships: the cooked files, the manifest and the materials' recipes, no other.
    for (const char* kind : {"textures", "meshes", "sounds", "skeletons", "clips"}) {
        fs::remove_all(project.root / "library" / kind);
    }
    fs::remove(Project::cookedOf(AssetType::AudioClip, "assets/chime.wav"));

    // What the runtime and the server are: no import installed, the manifest trusted.
    assetFactory() = AssetFactory{};
    AssetLibrary::get().load(AssetLibrary::Truth::Manifest);
    ResourceManager loaded;
    CHECK(SceneAssets::loadNamed(AssetType::Material, "wall", loaded));
    CHECK(SceneAssets::loadNamed(AssetType::Skeleton, "assets/rig.gltf:skeleton", loaded));
    CHECK(SceneAssets::loadNamed(AssetType::Mesh, "assets/rig.gltf:mesh0", loaded));
    CHECK(!SceneAssets::loadNamed(AssetType::AudioClip, "assets/chime.wav", loaded));
    REQUIRE(awaitAsyncLoads(loaded));
    const TextureHandle wall = loaded.findByName<TextureAsset>("assets/wall.png");
    REQUIRE(wall);
    CHECK(loaded.get(loaded.findByName<MaterialAsset>("wall")).albedoTexture == wall);
    CHECK(loaded.get(wall).params.mipLevels == 4);
    CHECK(loaded.get(loaded.findByName<MeshAsset>("assets/rig.gltf:mesh0")).indices.size() == 36);
    CHECK(countInLog("'assets/chime.wav': no cooked file this build can read, and this host imports no "
                     "recipes") == 1);
}

TEST("the explicit cook bakes again what its source art changed under, and only that") {
    Project project;
    REQUIRE(AssetCooker::cookAllAssets(project.resources));
    const fs::path before = Project::cookedOf(AssetType::Texture, "assets/wall.png");
    const std::string old = readText(before);

    // Nothing changed: nothing is baked.
    auto marks = markFiles(project.root);
    REQUIRE(AssetCooker::cookStaleAssets());
    CHECK(touched(marks).empty());

    // Re-exported art under an unchanged recipe.
    marks = markFiles(project.root);
    writeImage(project.root / "assets/wall.png", 8, 8, 4, wallTexels(1));
    REQUIRE(AssetCooker::cookStaleAssets());
    const fs::path after = Project::cookedOf(AssetType::Texture, "assets/wall.png");
    CHECK(after != before);
    std::error_code ec;
    CHECK(!fs::exists(before, ec));
    CHECK(AssetCook::isCookedCurrent(AssetType::Texture, after));
    CHECK(readText(after) != old);
    CHECK(touched(marks) == std::vector<std::string>{before.filename().string()});

    // A load of it now reads the new bytes.
    ResourceManager loaded;
    CHECK(SceneAssets::loadNamed(AssetType::Texture, "assets/wall.png", loaded));
    REQUIRE(awaitAsyncLoads(loaded));
    TextureAsset fresh;
    REQUIRE(AssetCook::readTexture(after, fresh));
    CHECK(loaded.get(loaded.findByName<TextureAsset>("assets/wall.png")).pixelData == fresh.pixelData);
}

TEST("a level of detail is baked again when the mesh it was simplified from changes") {
    const fs::path root = openProject();
    registerRecipeAssetFactories();
    AssetLibrary::get().load(AssetLibrary::Truth::Recipes);
    // A smooth sphere as an OBJ, triangle by triangle.
    const auto writeSphere = [&](float radius) {
        const MeshAsset sphere = generateSphere({8});
        std::string obj;
        for (const Vertex& v : sphere.vertices) {
            const glm::vec3 p = v.position * radius;
            obj += "v " + std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z) + "\n";
            obj += "vn " + std::to_string(v.normal.x) + " " + std::to_string(v.normal.y) + " "
                + std::to_string(v.normal.z) + "\n";
            obj += "vt " + std::to_string(v.uv.x) + " " + std::to_string(v.uv.y) + "\n";
        }
        for (size_t i = 0; i < sphere.indices.size(); i += 3) {
            obj += "f";
            for (size_t k = 0; k < 3; ++k) {
                const std::string at = std::to_string(sphere.indices[i + k] + 1);
                obj += " " + at + "/" + at + "/" + at;
            }
            obj += "\n";
        }
        writeText(root / "assets/rock.obj", obj);
    };
    writeSphere(1.0f);
    {
        ResourceManager resources;
        const MeshHandle rock = requestModelMeshAsync("assets/rock.obj", 0, resources);
        REQUIRE(awaitAsyncLoads(resources));
        const LOD lod = generateLOD(resources, rock, 1);
        REQUIRE(lod.levels.size() == 2);
        REQUIRE(AssetCooker::cookAllAssets(resources));
    }
    const std::string level    = LOD::levelName("assets/rock.obj:mesh0", 1);
    const fs::path baseBefore  = Project::cookedOf(AssetType::Mesh, "assets/rock.obj:mesh0");
    const fs::path levelBefore = Project::cookedOf(AssetType::Mesh, level);
    MeshAsset small;
    REQUIRE(AssetCook::readMesh(levelBefore, small));

    writeSphere(3.0f);
    REQUIRE(AssetCooker::cookStaleAssets());
    const fs::path levelAfter = Project::cookedOf(AssetType::Mesh, level);
    CHECK(Project::cookedOf(AssetType::Mesh, "assets/rock.obj:mesh0") != baseBefore);
    CHECK(levelAfter != levelBefore);
    MeshAsset large;
    REQUIRE(AssetCook::readMesh(levelAfter, large));
    CHECK_NEAR(large.boundsMax.y, small.boundsMax.y * 3.0f, 1e-3f);
}

TEST("a cooked file damaged on disk is refused at load, dropped, and baked again by the next cook") {
    Project project;
    REQUIRE(AssetCooker::cookAllAssets(project.resources));
    const std::string cube = project.items[2].name;
    // Every byte of the bodies spoiled, the length left whole: the header passes, the body cannot.
    for (const auto& [type, name] : {std::pair{AssetType::AudioClip, std::string("assets/chime.wav")},
                                     std::pair{AssetType::Mesh, cube}}) {
        const fs::path path = Project::cookedOf(type, name);
        std::string bytes   = readText(path);
        for (size_t i = 20; i < bytes.size(); ++i) bytes[i] = '\xff';
        writeText(path, bytes);
        REQUIRE(AssetCook::isCookedCurrent(type, path));
    }
    const fs::path sound = Project::cookedOf(AssetType::AudioClip, "assets/chime.wav");
    const fs::path mesh  = Project::cookedOf(AssetType::Mesh, cube);

    // A sound is read at once: refused, it comes from its recipe instead.
    ResourceManager loaded;
    CHECK(SceneAssets::loadNamed(AssetType::AudioClip, "assets/chime.wav", loaded));
    CHECK(*loaded.get(loaded.findByName<AudioClipAsset>("assets/chime.wav")).samples == chimeSamples());
    // A mesh is read on a worker: refused, it is left empty this load and said.
    CHECK(SceneAssets::loadNamed(AssetType::Mesh, cube, loaded));
    CHECK(!awaitAsyncLoads(loaded));
    CHECK(loaded.get(loaded.findByName<MeshAsset>(cube)).vertices.empty());
    CHECK(countInLog("could not be read") == 2);
    std::error_code ec;
    CHECK(!fs::exists(sound, ec));
    CHECK(!fs::exists(mesh, ec));

    // The next cook finds them missing and bakes them again.
    REQUIRE(AssetCooker::cookStaleAssets());
    AudioClipAsset again;
    CHECK(AssetCook::readAudioClip(Project::cookedOf(AssetType::AudioClip, "assets/chime.wav"), again));
    MeshAsset rebuilt;
    CHECK(AssetCook::readMesh(Project::cookedOf(AssetType::Mesh, cube), rebuilt));
    CHECK(rebuilt.indices.size() == 36);
}

TEST("a cook fails on an asset whose source did not load, and skips one made in code, saying so") {
    const fs::path root = openProject();
    registerRecipeAssetFactories();
    AssetLibrary::get().load(AssetLibrary::Truth::Recipes);
    ResourceManager resources;
    TextureAsset lost;
    lost.sourceJson() = {{"kind", "file"}, {"path", "assets/gone.png"}, {"usage", "Color"}};
    resources.add(std::move(lost), "assets/gone.png");
    resources.add(generateTriangle(), "made:by:generator");

    CHECK(!AssetCooker::cookAllAssets(resources));
    CHECK(countInLog("'assets/gone.png' has a recipe but nothing to bake") == 1);
    CHECK(!AssetLibrary::get().find(AssetType::Texture, "assets/gone.png"));
    // One failure costs its own record, not the others'.
    CHECK(AssetLibrary::get().find(AssetType::Mesh, "made:by:generator"));

    ResourceManager code;
    MeshAsset bare;
    bare.vertices = generateTriangle().vertices;
    bare.indices  = {0, 1, 2};
    code.add(std::move(bare), "made:by:hand");
    CHECK(AssetCooker::cookAllAssets(code));
    CHECK(!AssetLibrary::get().find(AssetType::Mesh, "made:by:hand"));
    CHECK(countInLog("'made:by:hand' has nothing to cook and no library entry") == 1);
}

namespace {

// What COOKER_VERSION 19 bakes from the fixed inputs below, on every platform. A change to anything
// the cooker writes moves it: bump COOKER_VERSION, then pin the pair the test prints.
constexpr uint32_t PINNED_COOKER_VERSION = 19;
constexpr uint64_t PINNED_COOKER_OUTPUT  = 0x229f8c1c9dcc123dull;

struct Hash {
    uint64_t value = FNV1A_OFFSET_BASIS;
    template<typename T>
    void add(const std::vector<T>& values) {
        value = fnv1a64Bytes(values.data(), values.size() * sizeof(T), value);
    }
    void add(const std::string& bytes) { value = fnv1a64Bytes(bytes.data(), bytes.size(), value); }
};

/// A texture of @p params' one level, its bytes counted off its format.
TextureAsset texture(TextureParams params) {
    TextureAsset out;
    params.width     = 4;
    params.height    = 4;
    params.mipLevels = 1;
    out.params       = params;
    out.pixelData.resize(static_cast<size_t>(textureLevelBytes(params, 0)));
    for (size_t i = 0; i < out.pixelData.size(); ++i) out.pixelData[i] = static_cast<uint8_t>(i * 31);
    return out;
}

} // namespace

TEST("what the cooker writes is pinned to its version") {
    openProject();
    Hash hash;

    // Every enumerator a cooked texture holds, by name: one renumbered moves its byte.
    std::vector<TextureParams> every;
    using F = TextureInternalFormat;
    const F formats[] = {
        F::R8, F::RG8, F::RGB8, F::RGBA8, F::SRGB8, F::SRGBA8, F::RGBA16F, F::RGBA32F,
        F::BC4R, F::BC5RG, F::BC7RGBA, F::BC7SRGBA,
    };
    for (const F format : formats) {
        TextureParams p;
        p.internalFormat = format;
        every.push_back(p);
    }
    for (const TexturePixelFormat format :
         {TexturePixelFormat::R, TexturePixelFormat::RG, TexturePixelFormat::RGB, TexturePixelFormat::RGBA}) {
        TextureParams p;
        p.format = format;
        every.push_back(p);
    }
    for (const TexturePixelType type :
         {TexturePixelType::UnsignedByte, TexturePixelType::Float, TexturePixelType::HalfFloat}) {
        TextureParams p;
        p.type = type;
        every.push_back(p);
    }
    const TextureWrapMode wraps[] = {
        TextureWrapMode::Repeat,
        TextureWrapMode::MirroredRepeat,
        TextureWrapMode::ClampToEdge,
        TextureWrapMode::ClampToBorder,
    };
    for (const TextureWrapMode wrap : wraps) {
        TextureParams p;
        p.wrapS = wrap;
        p.wrapT = wrap;
        every.push_back(p);
    }
    for (const TextureFilterOverride filter : {TextureFilterOverride::None, TextureFilterOverride::Nearest}) {
        TextureParams p;
        p.filterOverride = filter;
        every.push_back(p);
    }
    const fs::path cooked = scratch() / "pin.vkmc";
    for (const TextureParams& params : every) {
        REQUIRE(AssetCook::writeTexture(cooked, texture(params)));
        hash.add(readText(cooked));
    }

    // What a cook decides itself: a texture's chain and blocks, a mesh's order, a lightmap's
    // unwrap, a level's simplification, an import's weld and tangents.
    const std::vector<std::pair<F, int>> sources = {{F::SRGBA8, 4}, {F::RGB8, 3}, {F::R8, 1}, {F::RG8, 2}};
    for (const auto& [format, channels] : sources) {
        TextureAsset source;
        source.params.width          = 16;
        source.params.height         = 8;
        source.params.internalFormat = format;
        source.params.format         = inferFormat(channels);
        source.pixelData.resize(16 * 8 * channels);
        for (size_t i = 0; i < source.pixelData.size(); ++i) {
            source.pixelData[i] = static_cast<uint8_t>((i * i * 7 + i * 13) & 0xFF);
        }
        TextureAsset baked;
        REQUIRE(AssetCooker::bakeTexture(source, baked, format == F::RGB8 ? &source : nullptr));
        REQUIRE(AssetCook::writeTexture(cooked, baked));
        hash.add(readText(cooked));
    }
    MeshAsset sphere = generateSphere({6});
    unwrapLightmap(sphere, "pin");
    optimizeMeshForGpu(sphere);
    REQUIRE(AssetCook::writeMesh(cooked, sphere));
    hash.add(readText(cooked));
    const MeshAsset level = decimateMesh(generateSphere({12}), 0.3f);
    REQUIRE(AssetCook::writeMesh(cooked, level));
    hash.add(readText(cooked));

    writeText(Roots::projectRoot() / "rig.gltf", riggedGltf());
    writeText(Roots::projectRoot() / "box.obj", objText(boxFaces({0, 0, 0}, {1, 2, 3})));
    for (const char* file : {"rig.gltf", "box.obj"}) {
        const auto model = parseModel((Roots::projectRoot() / file).string(), file);
        REQUIRE(model);
        for (const SourceMesh& mesh : model->meshes) {
            REQUIRE(AssetCook::writeMesh(cooked, mesh.geometry));
            hash.add(readText(cooked));
        }
    }
    ResourceManager resources;
    writeText(Roots::projectRoot() / "assets/rig.gltf", riggedGltf());
    const SkeletonHandle rig = loadModelSkeleton("assets/rig.gltf", resources);
    const AnimationClipHandle clip =
        loadModelAnimationClip("assets/rig.gltf", 0, {{"step", 0.5f}}, resources);
    REQUIRE(AssetCook::writeSkeleton(cooked, resources.get(rig)));
    hash.add(readText(cooked));
    REQUIRE(AssetCook::writeAnimationClip(cooked, resources.get(clip)));
    hash.add(readText(cooked));
    AudioClipAsset sound;
    sound.sampleRate = 22050;
    sound.channels   = 2;
    sound.samples    = ClipSamples(chimeSamples());
    REQUIRE(AssetCook::writeAudioClip(cooked, sound));
    hash.add(readText(cooked));

    std::printf(
        "  COOKER_VERSION %u bakes %016llx\n",
        AssetCook::COOKER_VERSION,
        static_cast<unsigned long long>(hash.value)
    );
    REQUIRE(AssetCook::COOKER_VERSION == PINNED_COOKER_VERSION);
    CHECK(hash.value == PINNED_COOKER_OUTPUT);
}
