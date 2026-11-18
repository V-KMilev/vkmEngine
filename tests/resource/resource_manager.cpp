// The asset graph as a game and the editor use it: assets added, found and changed by name and by
// handle, each kind's place ready before anything is added to it.

#include <string>

#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/font_asset.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/asset/terrain_asset.h"
#include "resource/asset/texture_asset.h"
#include "resource/asset_ref.h"
#include "resource/resource_manager.h"

#include "harness/check.h"

using namespace Vkm::Engine;

namespace {

/// A one-triangle mesh, told apart by its first corner's x.
MeshAsset triangle(float x) {
    MeshAsset mesh;
    mesh.vertices.resize(3);
    mesh.vertices[0].position.x = x;
    mesh.indices                = {0, 1, 2};
    return mesh;
}

/// How many assets of every kind @p resources holds; read through a const manager, which makes no
/// place for a kind.
size_t everyKind(const ResourceManager& resources) {
    size_t count = 0;
#define VKM_TEST_COUNT_KIND(tag, Asset, name, dir, cooked) count += resources.countOfType<Asset>();
    VKM_ASSET_KINDS(VKM_TEST_COUNT_KIND)
#undef VKM_TEST_COUNT_KIND
    return count + resources.countOfType<FontAsset>();
}

} // namespace

TEST("an asset is found by name and by handle, and a removed one's handle goes stale") {
    ResourceManager resources;
    const MeshHandle wall = resources.add(triangle(1.0f), "wall");
    REQUIRE(wall);
    CHECK(resources.findByName<MeshAsset>("wall") == wall);
    CHECK(resources.find(AssetRef<MeshAsset>{"wall"}) == wall);
    CHECK(resources.get(wall).name() == "wall");
    CHECK(resources.get(wall).vertices[0].position.x == 1.0f);
    CHECK(!resources.findByName<MeshAsset>("floor"));
    CHECK(!resources.find(AssetRef<MeshAsset>{}));
    // A name is one asset within its kind; another kind may use it.
    CHECK(!resources.findByName<TextureAsset>("wall"));

    resources.remove(wall);
    CHECK(!resources.isAlive(wall));
    CHECK(resources.tryGet(wall) == nullptr);
    CHECK(!resources.findByName<MeshAsset>("wall"));

    // The next asset may take the freed slot; the old handle must not reach it.
    const MeshHandle next = resources.add(triangle(2.0f), "next");
    CHECK(resources.isAlive(next));
    CHECK(!resources.isAlive(wall));
    CHECK(resources.tryEdit(wall) == nullptr);
    CHECK(resources.countOfType<MeshAsset>() == 1);
}

TEST("adding under a taken name replaces the asset in place, its handle kept and its version moved on") {
    ResourceManager resources;
    const MeshHandle first = resources.add(triangle(1.0f), "crate");
    const uint64_t   uid   = resources.get(first).uid();
    const uint64_t   seen  = resources.get(first).version();

    // As a behavior's onStart does again after a script reload.
    const MeshHandle again = resources.add(triangle(2.0f), "crate");
    CHECK(again == first);
    CHECK(resources.countOfType<MeshAsset>() == 1);
    CHECK(resources.get(first).vertices[0].position.x == 2.0f);
    CHECK(resources.get(first).name() == "crate");
    // A version the backend has seen is never replayed; a fresh uid tells an old decode apart.
    CHECK(resources.get(first).version() > seen);
    CHECK(resources.get(first).uid() != uid);
}

TEST("a committed or swapped asset moves its version and its kind's revision, and no other kind's") {
    ResourceManager resources;
    const MeshHandle mesh = resources.add(triangle(1.0f), "mesh");
    const TextureHandle texture = resources.add(TextureAsset{}, "texture");

    const uint64_t version  = resources.get(mesh).version();
    const uint64_t graph    = resources.revision();
    const uint64_t meshes   = resources.revisionOf<MeshAsset>();
    const uint64_t textures = resources.revisionOf<TextureAsset>();

    // An edit is the caller's until it commits: the backend uploads on a version it has not seen.
    resources.edit(mesh).vertices[0].position.x = 5.0f;
    CHECK(resources.get(mesh).version() == version);
    resources.commit(mesh);
    CHECK(resources.get(mesh).version() == version + 1);
    CHECK(resources.revision() > graph);
    CHECK(resources.revisionOf<MeshAsset>() > meshes);
    CHECK(resources.revisionOf<TextureAsset>() == textures);

    // swapValue rebuilds the contents under the same name, handle and uid.
    const uint64_t uid = resources.get(mesh).uid();
    MeshAsset rebuilt  = triangle(9.0f);
    resources.swapValue(mesh, rebuilt);
    CHECK(resources.get(mesh).vertices[0].position.x == 9.0f);
    CHECK(resources.get(mesh).name() == "mesh");
    CHECK(resources.get(mesh).uid() == uid);
    CHECK(resources.get(mesh).version() == version + 2);
    CHECK(rebuilt.vertices[0].position.x == 5.0f);
    CHECK(resources.findByName<MeshAsset>("mesh") == mesh);
    CHECK(resources.revisionOf<TextureAsset>() == textures);
    CHECK(resources.isAlive(texture));
}

TEST("an unnamed asset is called asset, then asset (2), and a rename onto a taken name is suffixed, never replacing") {
    ResourceManager resources;
    const MeshHandle a = resources.add(triangle(1.0f));
    const MeshHandle b = resources.add(triangle(2.0f));
    CHECK(resources.get(a).name() == "asset");
    CHECK(resources.get(b).name() == "asset (2)");
    CHECK(a != b);

    const MeshHandle door = resources.add(triangle(3.0f), "door");
    // A text field's slip: renaming onto another's name must not destroy it.
    resources.rename(b, "door");
    CHECK(resources.get(b).name() == "door (2)");
    CHECK(resources.findByName<MeshAsset>("door") == door);
    CHECK(resources.get(door).vertices[0].position.x == 3.0f);
    CHECK(resources.findByName<MeshAsset>("door (2)") == b);
    CHECK(!resources.findByName<MeshAsset>("asset (2)"));

    resources.rename(a, "");
    CHECK(resources.get(a).name() == "asset");
    CHECK(resources.countOfType<MeshAsset>() == 3);
}

TEST("findOrAdd makes an asset once and keeps what stands under its name") {
    ResourceManager resources;
    int made = 0;
    const auto make = [&] {
        ++made;
        return triangle(static_cast<float>(made));
    };
    const MeshHandle first  = resources.findOrAdd<MeshAsset>("game:red", make);
    const MeshHandle second = resources.findOrAdd<MeshAsset>("game:red", make);
    CHECK(first == second);
    CHECK(made == 1);
    CHECK(resources.get(first).vertices[0].position.x == 1.0f);
    CHECK(resources.get(first).name() == "game:red");
}

TEST("a new manager has a place for every asset kind, and keeps it through a clear and a swap") {
    // A const manager reads a kind's place and never makes one, so a kind without one fails here.
    const ResourceManager fresh;
    CHECK(everyKind(fresh) == 0);
    CHECK(!fresh.findByName<AudioClipAsset>("chime"));

    ResourceManager live;
    ResourceManager staging;
    live.add(triangle(1.0f), "old");
    const Handle<FontAsset> font = live.add(FontAsset{}, "ui:font");
    staging.add(triangle(2.0f), "new");
    staging.add(SkeletonAsset{}, "rig");

    // A scene load commits a staging graph by swap; the fonts the host baked stay where they were.
    const uint64_t epoch = live.epoch();
    live.swap(staging);
    CHECK(live.epoch() != epoch);
    CHECK(live.findByName<MeshAsset>("new"));
    CHECK(live.findByName<SkeletonAsset>("rig"));
    CHECK(!live.findByName<MeshAsset>("old"));
    CHECK(staging.findByName<MeshAsset>("old"));
    CHECK(live.findByName<FontAsset>("ui:font") == font);
    CHECK(staging.countOfType<FontAsset>() == 0);

    live.clear();
    CHECK(everyKind(live) == 1);
    CHECK(live.findByName<FontAsset>("ui:font") == font);
    const ResourceManager& cleared = live;
    CHECK(everyKind(cleared) == 1);
    CHECK(everyKind(staging) == 1);
}
