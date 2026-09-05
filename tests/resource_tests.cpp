#include "support.h"

namespace {

// The engine's assets are owned once, referenced by a generational handle, and
// serialized by name - three claims `guides/engine.md` calls load-bearing and
// which nothing tested. These are the properties every scene load leans on.

void testHandlesAndNames() {
    std::printf("What a handle and a name each promise:\n");

    ResourceManager resources;

    const MeshHandle cube = resources.add(generateCube(), "test:cube");
    check("adding an asset yields a live handle", bool(cube) && resources.isAlive(cube));
    check("and it kept the name it was given", resources.get(cube).name() == "test:cube");

    check("a name resolves back to the same handle",
          resources.findByName<MeshAsset>("test:cube") == cube);
    check("and a name nobody took resolves to nothing",
          !resources.findByName<MeshAsset>("test:absent"));

    // The name is the identity that crosses a save, so two assets answering to
    // one name would make a scene file ambiguous about which it meant.
    const MeshHandle second = resources.add(generateSphere(), "test:sphere");
    check("a second asset is its own handle", second != cube);
    check("and does not answer to the first's name",
          resources.findByName<MeshAsset>("test:cube") == cube);

    resources.rename(second, "test:renamed");
    check("a rename moves the name", !resources.findByName<MeshAsset>("test:sphere"));
    check("  to the new one",
          resources.findByName<MeshAsset>("test:renamed") == second);
    check("  and the handle is unchanged by it", resources.isAlive(second));
}

void testAHandleOutlivingItsAsset() {
    std::printf("A handle held past the asset under it:\n");

    ResourceManager resources;
    const MeshHandle mesh = resources.add(generateCube(), "test:doomed");
    check("it starts alive", resources.isAlive(mesh));

    resources.remove(mesh);
    check("and is not alive once removed", !resources.isAlive(mesh));
    check("and its name is free again", !resources.findByName<MeshAsset>("test:doomed"));

    // The generation is the whole point: a new asset landing in the freed slot
    // must not answer to the old handle, or a stale reference silently reads
    // whatever replaced what it meant.
    const MeshHandle reused = resources.add(generateSphere(), "test:doomed");
    check("a new asset may take the freed name", bool(reused));
    check("but the old handle is still dead", !resources.isAlive(mesh));
    check("and the two handles are not equal", !(reused == mesh));
}

void testTypesDoNotShareASpace() {
    std::printf("Two asset kinds under one manager:\n");

    ResourceManager resources;
    const MeshHandle    mesh    = resources.add(generateCube(), "shared:name");
    const TextureHandle white = generateWhiteTexture(resources);
    resources.rename(white, "shared:name");
    const TextureHandle texture = white;

    // Names are per-kind: the scene writes which kind it wants beside the name,
    // so a mesh and a texture called the same thing is not a collision.
    check("both exist", resources.isAlive(mesh) && resources.isAlive(texture));
    check("the mesh name finds the mesh",
          resources.findByName<MeshAsset>("shared:name") == mesh);
    check("the texture name finds the texture",
          resources.findByName<TextureAsset>("shared:name") == texture);

    resources.remove(mesh);
    check("removing one leaves the other", resources.isAlive(texture));
    check("  and its name still resolves",
          resources.findByName<TextureAsset>("shared:name") == texture);
}

void testEditingInPlace() {
    std::printf("Editing an asset through its handle:\n");

    ResourceManager resources;
    const MeshHandle mesh = resources.add(generateCube(), "test:edited");
    const size_t before = resources.get(mesh).vertices.size();

    resources.edit(mesh).vertices.clear();
    check("edit reaches the same asset get sees", resources.get(mesh).vertices.empty());
    check("  and it was not empty to begin with", before > 0);
    check("  and the handle still names it", resources.isAlive(mesh));
}

} // namespace

void runResourceTests() {
    testHandlesAndNames();
    testAHandleOutlivingItsAsset();
    testTypesDoNotShareASpace();
    testEditingInPlace();
}
