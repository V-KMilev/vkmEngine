// The asset kinds as the project's files lay them out: each kind's recipes and cooked files in a
// folder of its own, under the key its section of a scene's assets block takes.

#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "io/asset/asset_library.h"
#include "io/scene/scene_assets.h"
#include "resource/asset_type.h"
#include "resource/resource_manager.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;

TEST("each asset kind keeps its recipes and cooked files in its own folder, named as its scene section") {
    const auto root = openProject();
    std::set<std::string> folders;
    std::set<uint16_t>    tags;
    // One name in every kind: the kind alone tells their files apart.
#define VKM_TEST_KIND(tag, Asset, kindName, dir, cooked)                                                    \
    {                                                                                                       \
        const AssetType type = AssetType::tag;                                                              \
        CHECK(AssetLibrary::recipePath(type, "x").parent_path() == root / "library" / #dir);                \
        CHECK(AssetLibrary::cookedPath(type, "x", 7).parent_path() == root / "cooked" / #dir);              \
        CHECK(std::string(Reflect::enumName(type)) == kindName);                                            \
        folders.insert(#dir);                                                                               \
        if (COOKED_KIND<Asset> != 0) CHECK(tags.insert(COOKED_KIND<Asset>).second);                         \
    }
    VKM_ASSET_KINDS(VKM_TEST_KIND)
#undef VKM_TEST_KIND
    CHECK(folders.size() == static_cast<size_t>(AssetType::Count));
    CHECK(tags.size() == static_cast<size_t>(
        AssetType::Count
    ) - 2);  // a material and a terrain have no cooked file

    const ResourceManager empty;
    const nlohmann::json block = SceneAssets::saveAllAssets(empty);
    std::set<std::string> sections;
    for (auto it = block.begin(); it != block.end(); ++it) sections.insert(it.key());
    CHECK(sections == folders);

    // Two names of one kind, and one name of two kinds, are four files.
    std::set<std::string> files;
    for (const AssetType type : {AssetType::Mesh, AssetType::Texture}) {
        for (const char* name : {"a", "b"}) files.insert(AssetLibrary::recipePath(type, name).string());
    }
    CHECK(files.size() == 4);
}
