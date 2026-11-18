// Meshes and textures decoded on the thread pool as a game streams them in: what the worker read
// lands on the asset in a frame, on the main thread, and never on an asset that is no longer the
// one it was asked for.

#include <chrono>
#include <thread>
#include <vector>

#include "import/model/model_import.h"
#include "import/texture_import.h"
#include "resource/async_load_queue.h"

#include "harness/check.h"
#include "harness/world.h"
#include "pipeline/art.h"

using namespace Vkm::Test;

namespace {

/**
 * @brief Wait until the pool has finished @p meshes and @p textures decodes, leaving every one
 *        waiting to be landed.
 *
 * Bounded only so a dead worker fails the test rather than hanging it.
 */
void waitForDecodes(size_t meshes, size_t textures) {
    AsyncLoadQueue& queue = AsyncLoadQueue::get();
    std::vector<MeshLoadCompletion>    doneMeshes;
    std::vector<TextureLoadCompletion> doneTextures;
    const auto patience = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while ((doneMeshes.size() < meshes || doneTextures.size() < textures)
           && std::chrono::steady_clock::now() < patience) {
        for (MeshLoadCompletion& done : queue.drainMeshes()) doneMeshes.push_back(std::move(done));
        for (TextureLoadCompletion& done : queue.drainTextures()) doneTextures.push_back(std::move(done));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const size_t gotMeshes = doneMeshes.size(), gotTextures = doneTextures.size();
    for (MeshLoadCompletion& done : doneMeshes) queue.pushMesh(std::move(done));
    for (TextureLoadCompletion& done : doneTextures) queue.pushTexture(std::move(done));
    REQUIRE(gotMeshes == meshes);
    REQUIRE(gotTextures == textures);
}

std::vector<uint8_t> texels() {
    std::vector<uint8_t> out(8 * 8 * 4);
    for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<uint8_t>(i * 5);
    return out;
}

} // namespace

TEST("a mesh and a texture loaded in the background stay empty until a frame lands them") {
    const auto root = openProject();
    writeText(root / "assets/box.obj", objText(boxFaces({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f})));
    writeImage(root / "assets/wall.png", 8, 8, 4, texels());

    Game game;
    ResourceManager& resources = game.resources();
    const MeshHandle mesh = requestModelMeshAsync("assets/box.obj", 0, resources);
    const TextureHandle texture = requestTextureAsync("assets/wall.png", resources, TextureUsage::Color);
    REQUIRE(mesh);
    REQUIRE(texture);
    // Asked for twice, it is the one load.
    CHECK(requestModelMeshAsync("assets/box.obj", 0, resources) == mesh);
    CHECK(requestTextureAsync("assets/wall.png", resources, TextureUsage::Color) == texture);
    const uint64_t meshVersion    = resources.get(mesh).version();
    const uint64_t textureVersion = resources.get(texture).version();

    // Read on the pool, not yet landed: the graph is the main thread's alone.
    waitForDecodes(1, 1);
    CHECK(resources.get(mesh).loading);
    CHECK(resources.get(mesh).vertices.empty());
    CHECK(resources.get(texture).loading);
    CHECK(resources.get(texture).pixelData.empty());

    game.run(1);
    const MeshAsset&    box  = resources.get(mesh);
    const TextureAsset& wall = resources.get(texture);
    CHECK(!box.loading);
    CHECK(box.indices.size() == 36);
    CHECK(box.version() > meshVersion);
    CHECK(box.name() == "assets/box.obj:mesh0");
    CHECK(box.hasSource());
    CHECK(!wall.loading);
    CHECK(wall.params.width == 8);
    CHECK(wall.pixelData.size() == texels().size());
    CHECK(wall.isSrgb());
    CHECK(wall.version() > textureVersion);
}

TEST("a decode for an asset replaced or removed while it ran is dropped, and the others land") {
    const auto root = openProject();
    writeText(root / "assets/box.obj", objText(boxFaces({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f})));
    writeImage(root / "assets/a.png", 8, 8, 4, texels());
    writeImage(root / "assets/b.png", 8, 8, 4, texels());

    Game game;
    ResourceManager& resources = game.resources();
    const MeshHandle mesh = requestModelMeshAsync("assets/box.obj", 0, resources);
    const TextureHandle dropped = requestTextureAsync("assets/a.png", resources, TextureUsage::Color);
    const TextureHandle kept    = requestTextureAsync("assets/b.png", resources, TextureUsage::Color);

    // The name taken by something else, as a scene load or a script reload does: a new asset.
    MeshAsset other;
    other.vertices.resize(3);
    other.indices = {0, 1, 2};
    REQUIRE(resources.add(std::move(other), "assets/box.obj:mesh0") == mesh);
    resources.remove(dropped);

    waitForDecodes(1, 2);
    game.run(1);
    CHECK(resources.get(mesh).indices.size() == 3);
    CHECK(!resources.get(mesh).loading);
    CHECK(!resources.isAlive(dropped));
    CHECK(resources.countOfType<TextureAsset>() == 1);
    CHECK(!resources.get(kept).loading);
    CHECK(resources.get(kept).pixelData.size() == texels().size());
}

TEST("a decode that fails leaves its asset empty and no longer loading, and says so") {
    const auto root = openProject();
    writeText(root / "assets/bad.png", "not an image");

    Game game;
    ResourceManager& resources = game.resources();
    const TextureHandle texture = requestTextureAsync("assets/bad.png", resources, TextureUsage::Data);
    const MeshHandle mesh = requestModelMeshAsync("assets/missing.obj", 0, resources);
    waitForDecodes(1, 1);
    game.run(1);
    CHECK(!resources.get(texture).loading);
    CHECK(resources.get(texture).pixelData.empty());
    CHECK(!resources.get(mesh).loading);
    CHECK(resources.get(mesh).vertices.empty());
    CHECK(countInLog("assets/bad.png: its file could not be read") == 1);
    CHECK(countInLog("assets/missing.obj:mesh0: its file could not be read") == 1);
}
