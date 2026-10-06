#include "support.h"

#include <thread>

#include <nlohmann/json.hpp>

#include "resource/texture_format.h"
#include "system/async/async_load_queue.h"
#include "system/async/async_loader_system.h"

namespace {

// The queue is process-wide, so every case opens by emptying it.
void emptyQueue() {
    AsyncLoadQueue::get().drainTextures();
    AsyncLoadQueue::get().drainMeshes();
}

TextureHandle loadingTexture(ResourceManager& resources, const char* name) {
    TextureAsset asset;
    asset.loading = true;
    return resources.add(std::move(asset), name);
}

TextureLoadCompletion decodedPixels(TextureHandle handle, uint64_t uid) {
    TextureAsset decoded;
    decoded.params.width          = 2;
    decoded.params.height         = 2;
    decoded.params.internalFormat = TextureInternalFormat::RGBA8;
    decoded.pixelData             = std::vector<uint8_t>(2 * 2 * 4, 0x7F);
    return {handle, uid, std::move(decoded)};
}

void testACompletionLandsOnItsAsset() {
    std::printf("What a finished decode does to the asset that asked for it:\n");
    emptyQueue();

    ResourceManager resources;
    const TextureHandle handle = loadingTexture(resources, "async:landed");
    const uint64_t version = resources.get(handle).version();

    AsyncLoadQueue::get().pushTexture(decodedPixels(handle, resources.get(handle).uid()));
    finalizeAsyncLoads(resources);

    const TextureAsset& asset = resources.get(handle);
    check("the pixels arrive", asset.pixelData.size() == 2 * 2 * 4);
    check("  the size comes with them", asset.params.width == 2 && asset.params.height == 2);
    check("  it stops reporting itself as loading", !asset.loading);
    check("  and the version moves, so the backend re-uploads", asset.version() > version);
}

void testADrainIsTotal() {
    std::printf("What a second drain finds:\n");
    emptyQueue();

    ResourceManager resources;
    const TextureHandle handle = loadingTexture(resources, "async:drained");
    AsyncLoadQueue::get().pushTexture(decodedPixels(handle, resources.get(handle).uid()));

    finalizeAsyncLoads(resources);
    const uint64_t settled = resources.get(handle).version();
    finalizeAsyncLoads(resources);

    check("nothing is applied twice", resources.get(handle).version() == settled);
}

void testAHandleFreedBeforeTheDrainIsDropped() {
    std::printf("What happens to a decode whose asset went away first:\n");
    emptyQueue();

    ResourceManager resources;
    const TextureHandle handle = loadingTexture(resources, "async:deleted");
    AsyncLoadQueue::get().pushTexture(decodedPixels(handle, resources.get(handle).uid()));

    // Delete an import still decoding, so the completion names a freed slot.
    resources.remove(handle);
    finalizeAsyncLoads(resources);

    check("the completion is dropped rather than applied", !resources.isAlive(handle));
}

void testASlotReusedByAnotherAssetDropsTheCompletion() {
    std::printf("What a decode does when its slot changed hands:\n");
    emptyQueue();

    ResourceManager resources;
    const TextureHandle handle = loadingTexture(resources, "async:recycled");
    const uint64_t requested = resources.get(handle).uid();
    AsyncLoadQueue::get().pushTexture(decodedPixels(handle, requested));

    // Adding under a taken name replaces the asset in place, so isAlive() still says
    // yes; only the uid recorded at request time tells the new occupant apart.
    TextureAsset replacement;
    replacement.loading = true;
    const TextureHandle same = resources.add(std::move(replacement), "async:recycled");
    check("the slot is the same one", same == handle);
    check("  but it is a different asset", resources.get(handle).uid() != requested);

    finalizeAsyncLoads(resources);

    const TextureAsset& asset = resources.get(handle);
    check("the stranger's pixels are not applied", asset.pixelData.empty());
    check("  and it is left waiting for its own decode", asset.loading);
}

void testAFailedDecodeStopsLoadingWithoutCommitting() {
    std::printf("What a decode that failed leaves behind:\n");
    emptyQueue();

    ResourceManager resources;
    const TextureHandle handle = loadingTexture(resources, "async:failed");
    const uint64_t version = resources.get(handle).version();

    AsyncLoadQueue::get().pushTexture({handle, resources.get(handle).uid(), TextureAsset{}});
    finalizeAsyncLoads(resources);

    const TextureAsset& asset = resources.get(handle);
    check("the asset is left empty", asset.pixelData.empty());
    check("  it stops reporting itself as loading, so nothing waits on it forever", !asset.loading);
    check("  and the version stands, so no re-upload is asked for", asset.version() == version);
}

void testACompletionKeepsItsOwnFormat() {
    std::printf("What a decoded texture's parameters survive:\n");
    emptyQueue();

    ResourceManager resources;
    const TextureHandle handle = loadingTexture(resources, "async:cooked");

    TextureLoadCompletion completion = decodedPixels(handle, resources.get(handle).uid());
    completion.decoded.params.internalFormat = TextureInternalFormat::SRGBA8;
    AsyncLoadQueue::get().pushTexture(std::move(completion));
    finalizeAsyncLoads(resources);

    const TextureAsset& asset = resources.get(handle);
    check(
        "the decode's format is taken as it stands",
        asset.params.internalFormat == TextureInternalFormat::SRGBA8
    );
    check("  and the colour space is read back off it rather than guessed", asset.isSrgb());
}

void testAMeshCompletionCarriesEveryFieldItTook() {
    std::printf("What travels from the worker's mesh to the live one:\n");
    emptyQueue();

    ResourceManager resources;
    MeshAsset stub;
    stub.loading      = true;
    stub.sourceJson() = {{"kind", "model"}};
    const MeshHandle handle = resources.add(std::move(stub), "async:mesh");

    MeshAsset decoded;
    decoded.vertices.resize(3);
    decoded.vertices[0].position = {1.0f, 2.0f, 3.0f};
    decoded.indices    = {0, 1, 2};
    decoded.skin.resize(3);
    decoded.skeleton   = "async:rig";
    decoded.boundsMin  = {-1.0f, -1.0f, -1.0f};
    decoded.boundsMax  = {1.0f, 1.0f, 1.0f};
    decoded.skinRadius = 0.5f;

    AsyncLoadQueue::get().pushMesh({handle, resources.get(handle).uid(), std::move(decoded)});
    finalizeAsyncLoads(resources);

    const MeshAsset& asset = resources.get(handle);
    check("the geometry arrives", asset.vertices.size() == 3 && asset.indices.size() == 3);
    check("  the rig binding with it", asset.skin.size() == 3 && asset.skeleton == "async:rig");
    check("  the bounds the worker measured", asset.boundsMax.x == 1.0f);
    check("  the skin radius visibility sizes a posed character by", asset.skinRadius == 0.5f);
    check("  and it is no longer loading", !asset.loading);
    check(
        "the slot keeps its name and the source it was requested with",
        asset.name() == "async:mesh" && asset.hasSource() && asset.sourceJson().value("kind", "") == "model"
    );
}

void testWaitingEndsWhenNothingIsInFlight() {
    std::printf("What awaiting the loads answers:\n");
    emptyQueue();

    ResourceManager resources;
    const TextureHandle handle = loadingTexture(resources, "async:awaited");
    AsyncLoadQueue::get().pushTexture(decodedPixels(handle, resources.get(handle).uid()));

    // The completion is already queued, so this settles at once, not at the timeout.
    check("it returns once the last in-flight asset has landed", awaitAsyncLoads(resources));
    check("  with the asset finished", !resources.get(handle).loading);
}

// The wait gives up after a stretch in which nothing lands, not after a fixed
// total, which a project with enough art outlasts while decodes are still arriving.
void testWaitingLastsAsLongAsLoadsKeepLanding() {
    std::printf("Waiting on loads that keep landing:\n");
    emptyQueue();

    ResourceManager resources;
    const TextureHandle first  = loadingTexture(resources, "async:first");
    const TextureHandle second = loadingTexture(resources, "async:second");
    const TextureLoadCompletion firstDone  = decodedPixels(first, resources.get(first).uid());
    const TextureLoadCompletion secondDone = decodedPixels(second, resources.get(second).uid());

    // Each lands within the patience of the one before; together they outlast it.
    const auto patience = std::chrono::milliseconds(300);
    std::thread worker([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        AsyncLoadQueue::get().pushTexture(firstDone);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        AsyncLoadQueue::get().pushTexture(secondDone);
    });
    const bool landed = awaitAsyncLoads(resources, patience);
    worker.join();

    check("a wait longer than the patience succeeds while loads keep landing", landed);
    check("  with both finished", !resources.get(first).loading && !resources.get(second).loading);

    ResourceManager stuck;
    loadingTexture(stuck, "async:never");
    check("and one with nothing landing gives up", !awaitAsyncLoads(stuck, std::chrono::milliseconds(50)));
}

// A scene load waits on its staging graph's imports while the live graph's are in
// the same queue. Draining everything would drop the live completions as strangers,
// and a failed load would leave the live graph with stubs that never finish.
void testWaitingOnOneGraphLeavesAnothersCompletions() {
    std::printf("Waiting on a staging graph while the live one is loading:\n");
    emptyQueue();

    ResourceManager live;
    const TextureHandle livePending = loadingTexture(live, "async:live");
    ResourceManager staging;
    const TextureHandle stagedPending = loadingTexture(staging, "async:staged");

    AsyncLoadQueue::get().pushTexture(decodedPixels(livePending, live.get(livePending).uid()));
    AsyncLoadQueue::get().pushTexture(decodedPixels(stagedPending, staging.get(stagedPending).uid()));

    check("the staging graph's wait ends", awaitAsyncLoads(staging));
    check("  with its own asset landed", !staging.get(stagedPending).loading);
    check("  and the live asset untouched by it", live.get(livePending).loading);

    finalizeAsyncLoads(live);
    check(
        "the live graph's next drain still lands its own",
        !live.get(livePending).loading && !live.get(livePending).pixelData.empty()
    );
}

} // namespace

void runAsyncTests() {
    testACompletionLandsOnItsAsset();
    testADrainIsTotal();
    testAHandleFreedBeforeTheDrainIsDropped();
    testASlotReusedByAnotherAssetDropsTheCompletion();
    testAFailedDecodeStopsLoadingWithoutCommitting();
    testACompletionKeepsItsOwnFormat();
    testAMeshCompletionCarriesEveryFieldItTook();
    testWaitingEndsWhenNothingIsInFlight();
    testWaitingOnOneGraphLeavesAnothersCompletions();
    testWaitingLastsAsLongAsLoadsKeepLanding();
}
