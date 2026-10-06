#include "net/net_support.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/math/random.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "io/asset/asset_cook.h"
#include "io/scene/scene_serializer.h"
#include "net/prediction/command.h"
#include "net/wire/bit_stream.h"
#include "net/wire/codecs.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/asset/texture_asset.h"
#include "resource/resource_manager.h"

namespace {

// Readers of bytes this process did not write, fed thousands of damaged copies of a
// good input. Each must never crash, and must accept only what the engine's own
// rules would accept from a writer; under the sanitizer, a one-byte overread fails.
//
// Seeded, so a failure reproduces: the seed and the iteration are printed.

constexpr uint64_t SEED       = 0x5eedf00dULL;
constexpr int      ITERATIONS = 400;
constexpr int      SCENE_SWAPS = 100;   // a scene-of-record load is the slow one

std::filesystem::path scratch(const char* name) {
    return runScratch() / name;
}

std::vector<uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// One of three kinds of damage: bytes changed in place, the tail cut off (a stopped
// transfer), or foreign bytes spliced in (a disk or a hostile peer).
std::vector<uint8_t> damage(const std::vector<uint8_t>& good, Math::Rng& rng) {
    std::vector<uint8_t> bytes = good;
    if (bytes.empty()) return bytes;
    switch (rng.nextU32() % 3) {
        case 0: {
            const uint32_t flips = 1 + rng.nextU32() % 8;
            for (uint32_t i = 0; i < flips; ++i) {
                bytes[rng.nextU32() % bytes.size()] = static_cast<uint8_t>(rng.nextU32());
            }
            break;
        }
        case 1:
            bytes.resize(rng.nextU32() % bytes.size());
            break;
        default: {
            const size_t at    = rng.nextU32() % bytes.size();
            const uint32_t run = 1 + rng.nextU32() % 16;
            std::vector<uint8_t> extra(run);
            for (uint8_t& b : extra) b = static_cast<uint8_t>(rng.nextU32());
            bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at), extra.begin(), extra.end());
            break;
        }
    }
    return bytes;
}

// Runs @p read over damaged copies of @p good and counts accepted results @p sound
// rejects. Accepting damage is fine - a flipped vertex byte is still a mesh -
// accepting what the rules refuse is not.
template <typename Asset, typename Read, typename Sound>
size_t acceptedButUnsound(
    const char* file,
    const std::vector<uint8_t>& good,
    Read read,
    Sound sound,
    size_t& accepted
) {
    const std::filesystem::path path = scratch(file);
    Math::Rng rng(SEED);
    size_t unsound = 0;
    for (int i = 0; i < ITERATIONS; ++i) {
        writeBytes(path, damage(good, rng));
        Asset out;
        if (!read(path, out)) continue;
        ++accepted;
        if (!sound(out)) {
            ++unsound;
            std::printf(
                "      iteration %d of seed %llx accepted an unsound %s\n",
                i,
                static_cast<unsigned long long>(SEED),
                file
            );
        }
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return unsound;
}

// Whether a texture holds exactly the bytes its upload reads, level by level. Worked
// out independently of the engine's size helpers, so their mistakes are not the judge's.
bool holdsWhatItsLevelsRead(const TextureAsset& texture) {
    const TextureParams& params = texture.params;
    if (params.width == 0 || params.height == 0 || texture.pixelData.empty()) return false;

    uint32_t chain = 1;
    for (uint32_t extent = std::max(params.width, params.height); extent > 1; extent >>= 1) ++chain;
    if (params.mipLevels != 1 && params.mipLevels != chain) return false;

    uint64_t block = 0;
    switch (params.internalFormat) {
        case TextureInternalFormat::BC4R:     block = 8;  break;
        case TextureInternalFormat::BC5RG:
        case TextureInternalFormat::BC7RGBA:
        case TextureInternalFormat::BC7SRGBA: block = 16; break;
        default:                              break;
    }
    const uint64_t channels = params.format == TexturePixelFormat::R ? 1
        : params.format == TexturePixelFormat::RG ? 2
        : params.format == TexturePixelFormat::RGB ? 3 : 4;
    const uint64_t component = params.type == TexturePixelType::HalfFloat ? 2
        : params.type == TexturePixelType::Float ? 4 : 1;

    uint64_t bytes = 0;
    for (uint32_t level = 0; level < params.mipLevels; ++level) {
        const uint64_t width  = std::max(params.width >> level, 1u);
        const uint64_t height = std::max(params.height >> level, 1u);
        bytes += block ? ((width + 3) / 4) * ((height + 3) / 4) * block
            : width * height * channels * component;
    }
    return bytes == texture.pixelData.size();
}

MeshAsset aMesh() {
    MeshAsset mesh;
    mesh.vertices = {
        Vertex{{0, 0, 0}, {0, 1, 0}, {0.0f, 0.0f}, {1, 0, 0, 1}},
        Vertex{{1, 0, 0}, {0, 1, 0}, {1.0f, 0.0f}, {1, 0, 0, 1}},
        Vertex{{0, 0, 1}, {0, 1, 0}, {0.0f, 1.0f}, {1, 0, 0, 1}},
        Vertex{{1, 0, 1}, {0, 1, 0}, {1.0f, 1.0f}, {1, 0, 0, 1}},
    };
    mesh.indices   = {0, 1, 2, 2, 1, 3};
    mesh.skin      = {
        SkinVertex{{0, 1, 0, 0}, {128, 127, 0, 0}},
        SkinVertex{{1, 0, 0, 0}, {255, 0, 0, 0}},
        SkinVertex{{0, 0, 0, 0}, {255, 0, 0, 0}},
        SkinVertex{{1, 0, 0, 0}, {255, 0, 0, 0}}
    };
    mesh.skeleton  = "rig:hostile";
    mesh.boundsMin = {0.0f, 0.0f, 0.0f};
    mesh.boundsMax = {1.0f, 0.0f, 1.0f};
    return mesh;
}

void testDamagedCookedFilesAreRefusedOrSound() {
    std::printf("Cooked files damaged %d ways each:\n", ITERATIONS);

    const std::filesystem::path good = scratch("vkm_hostile_good.vkmc");
    size_t accepted = 0;

    check("a mesh file writes", AssetCook::writeMesh(good, aMesh()));
    const size_t meshes = acceptedButUnsound<MeshAsset>(
        "vkm_hostile_mesh.vkmc",
        readBytes(good),
        AssetCook::readMesh,
        [](const MeshAsset& m) {
            for (uint32_t index : m.indices) if (index >= m.vertices.size()) return false;
            return m.skin.empty() || m.skin.size() == m.vertices.size();
        },
        accepted
    );
    check("  no mesh is read with an index past its vertices", meshes == 0);

    TextureAsset texture;
    texture.params.width  = 4;
    texture.params.height = 4;
    texture.params.format = TexturePixelFormat::RGBA;
    texture.params.type   = TexturePixelType::UnsignedByte;
    texture.params.internalFormat = TextureInternalFormat::SRGBA8;
    texture.pixelData.assign(64, 200);
    check("a texture file writes", AssetCook::writeTexture(good, texture));
    const size_t textures = acceptedButUnsound<TextureAsset>(
        "vkm_hostile_tex.vkmc",
        readBytes(good),
        AssetCook::readTexture,
        holdsWhatItsLevelsRead,
        accepted
    );
    check("  no texture is read with pixels its size cannot hold", textures == 0);

    // What the cooker writes: blocks, the whole chain. Any sixteen bytes are a block
    // as far as the file is concerned.
    TextureAsset blocks;
    blocks.params.width          = 8;
    blocks.params.height         = 8;
    blocks.params.internalFormat = TextureInternalFormat::BC7SRGBA;
    blocks.params.mipLevels      = 4;
    blocks.pixelData.assign(4 * 16 + 3 * 16, 0x5A);
    check("a compressed texture file writes", AssetCook::writeTexture(good, blocks));
    const size_t compressed = acceptedButUnsound<TextureAsset>(
        "vkm_hostile_bc.vkmc",
        readBytes(good),
        AssetCook::readTexture,
        holdsWhatItsLevelsRead,
        accepted
    );
    check("  no compressed texture is read with levels its bytes cannot hold", compressed == 0);

    SkeletonAsset skeleton;
    skeleton.bones       = { Bone{"hips", -1}, Bone{"spine", 0}, Bone{"head", 1} };
    skeleton.inverseBind = { glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f) };
    skeleton.bindPose    = { Transform{}, Transform{}, Transform{} };
    check("a skeleton file writes", AssetCook::writeSkeleton(good, skeleton));
    const size_t skeletons = acceptedButUnsound<SkeletonAsset>(
        "vkm_hostile_skel.vkmc",
        readBytes(good),
        AssetCook::readSkeleton,
        [](const SkeletonAsset& s) { return findSkeletonFault(s).empty(); },
        accepted
    );
    check("  no skeleton is read that its own rules refuse", skeletons == 0);

    AnimationClipAsset clip;
    clip.skeleton      = "rig:hostile";
    clip.duration      = 1.0f;
    clip.positionTimes = {0.0f, 0.5f, 1.0f};
    clip.positions     = {{0, 0, 0}, {0, 1, 0}, {0, 0, 0}};
    clip.bones.resize(3);
    clip.bones[2].position = {0, 3};
    clip.markers.push_back(ClipMarker{"step", 0.5f});
    check("a clip file writes", AssetCook::writeAnimationClip(good, clip));
    const size_t clips = acceptedButUnsound<AnimationClipAsset>(
        "vkm_hostile_clip.vkmc",
        readBytes(good),
        AssetCook::readAnimationClip,
        [](const AnimationClipAsset& c) { return findClipFault(c).empty(); },
        accepted
    );
    check("  no clip is read that its own rules refuse", clips == 0);

    AudioClipAsset audio;
    audio.sampleRate = 48000;
    audio.channels   = 2;
    audio.samples    = ClipSamples(std::vector<int16_t>(64, 1000));
    check("a sound file writes", AssetCook::writeAudioClip(good, audio));
    const size_t sounds = acceptedButUnsound<AudioClipAsset>(
        "vkm_hostile_audio.vkmc",
        readBytes(good),
        AssetCook::readAudioClip,
        [](const AudioClipAsset& a) {
            return a.channels > 0 && a.sampleRate > 0 && a.samples && a.samples->size() % a.channels == 0;
        },
        accepted
    );
    check("  no sound is read with a partial frame", sounds == 0);

    std::printf("      %zu damaged files were read, and every one was sound\n", accepted);
    std::error_code ec;
    std::filesystem::remove(good, ec);
}

void testADamagedSceneLoadsOrIsRefused() {
    std::printf("A saved scene damaged %d ways:\n", ITERATIONS);

    Scene scene;
    ResourceManager resources;
    for (int i = 0; i < 8; ++i) {
        const EntityId entity = scene.createEntity();
        scene.add(entity, makeName(("Crate " + std::to_string(i)).c_str()));
        Transform transform;
        transform.position = {float(i), 0.0f, 0.0f};
        scene.add(entity, transform);
    }
    const std::string text = SceneSerializer::saveToString(scene, resources);
    const std::vector<uint8_t> good(text.begin(), text.end());

    Math::Rng rng(SEED);
    size_t loaded = 0;
    bool escaped = false;
    for (int i = 0; i < ITERATIONS; ++i) {
        const std::vector<uint8_t> bytes = damage(good, rng);
        Scene into;
        ResourceManager intoResources;
        try {
            const std::string damaged(bytes.begin(), bytes.end());
            if (SceneSerializer::loadFromString(damaged, into, intoResources)) {
                ++loaded;
            }
        } catch (...) {
            escaped = true;
            std::printf(
                "      iteration %d of seed %llx threw out of the loader\n",
                i,
                static_cast<unsigned long long>(SEED)
            );
        }
    }
    std::printf("      %zu damaged scenes loaded\n", loaded);
    check("no damaged scene throws out of the loader", !escaped);

    // Damaged bytes rarely survive the JSON parser. A value swapped for the wrong type
    // or an absurd size does - what a hand-edited or hostile file carries.
    nlohmann::json doc = nlohmann::json::parse(text);
    // The shipped scene of record, when present: more component kinds than the crates.
    std::ifstream record(std::filesystem::path(VKM_EXAMPLES_DIR) / "physics_lab/scenes/obstacle_course.json");
    if (record) doc = nlohmann::json::parse(record, nullptr, false);
    if (doc.is_discarded()) doc = nlohmann::json::parse(text);
    std::vector<nlohmann::json::json_pointer> leaves;
    const std::function<void(const nlohmann::json&, const nlohmann::json::json_pointer&)> collect =
        [&](const nlohmann::json& node, const nlohmann::json::json_pointer& at) {
            if (node.is_object()) {
                for (auto it = node.begin(); it != node.end(); ++it) collect(it.value(), at / it.key());
            } else if (node.is_array()) {
                for (size_t i = 0; i < node.size(); ++i) collect(node[i], at / i);
            } else {
                leaves.push_back(at);
            }
        };
    collect(doc, nlohmann::json::json_pointer());

    const nlohmann::json hostile[] = {
        nullptr, -1, 0, 4294967295u, 1e38, -1e38, "x", nlohmann::json::array(),
        nlohmann::json::object(), true, 0.5, std::string(300, 'a'),
    };
    size_t swappedLoaded = 0;
    bool swappedEscaped = false;
    for (int i = 0; i < SCENE_SWAPS; ++i) {
        nlohmann::json swapped = doc;
        swapped[leaves[rng.nextU32() % leaves.size()]] =
            hostile[rng.nextU32() % (sizeof(hostile) / sizeof(hostile[0]))];
        Scene into;
        ResourceManager intoResources;
        try {
            if (SceneSerializer::loadFromString(swapped.dump(), into, intoResources)) ++swappedLoaded;
        } catch (...) {
            swappedEscaped = true;
            std::printf(
                "      value swap %d of seed %llx threw out of the loader\n",
                i,
                static_cast<unsigned long long>(SEED)
            );
        }
    }
    std::printf("      %zu of %d scenes with one value swapped loaded\n", swappedLoaded, SCENE_SWAPS);
    check("no value of the wrong type or size throws out of the loader", !swappedEscaped);
}

void testDamagedDatagramsAreRefusedOrBounded() {
    std::printf("Commands and spawns damaged %d ways:\n", ITERATIONS);

    std::vector<InputCommand> commands(4);
    for (uint32_t i = 0; i < commands.size(); ++i) commands[i].tick = 100 + i;
    std::vector<uint8_t> commandBytes;
    {
        BitWriter commandWriter(commandBytes, 512);
        writeCommands(commandWriter, commands, 0, 8);
        commandWriter.finish();
    }

    NetSpawn spawn;
    spawn.prefab      = "prefabs/crate.json";
    spawn.at.position = {3.0f, 4.0f, 5.0f};
    spawn.at.rotation = glm::angleAxis(0.7f, glm::normalize(glm::vec3(1.0f, 2.0f, 0.5f)));
    spawn.at.scale    = {2.0f, 0.5f, 3.25f};
    std::vector<uint8_t> spawnBytes;
    {
        BitWriter spawnWriter(spawnBytes, 512);
        netEncode(spawn, spawnWriter);
        spawnWriter.finish();
    }

    // What the codec's own rule would have written: an empty path is the
    // refusal, and anything else must be a pose a Transform can hold.
    const auto sound = [](const NetSpawn& got) {
        if (got.prefab.empty()) return true;
        const glm::vec3& s = got.at.scale;
        return got.prefab.size() <= NET_PREFAB_PATH_MAX && isSayable(got)
            && std::abs(s.x) <= NET_MAX_SCALE && std::abs(s.y) <= NET_MAX_SCALE
            && std::abs(s.z) <= NET_MAX_SCALE;
    };

    Math::Rng rng(SEED);
    bool overlong = false;
    size_t spawnsRead = 0;
    size_t spawnsUnsound = 0;
    for (int i = 0; i < ITERATIONS; ++i) {
        const std::vector<uint8_t> damagedCommands = damage(commandBytes, rng);
        BitReader commandReader(damagedCommands.data(), damagedCommands.size());
        std::vector<InputCommand> got;
        if (readCommands(commandReader, 8, got) && got.size() > NET_MAX_PACKET_COMMANDS) overlong = true;

        const std::vector<uint8_t> damagedSpawn = damage(spawnBytes, rng);
        BitReader spawnReader(damagedSpawn.data(), damagedSpawn.size());
        NetSpawn read;
        read.prefab = "left over from before";
        netDecode(read, spawnReader);
        if (!read.prefab.empty()) ++spawnsRead;
        if (!sound(read)) {
            ++spawnsUnsound;
            std::printf(
                "      iteration %d of seed %llx decoded an unsound spawn\n",
                i,
                static_cast<unsigned long long>(SEED)
            );
        }
    }
    std::printf("      %zu damaged spawns were believed, the rest refused\n", spawnsRead);
    check("no damaged packet yields more commands than a packet may carry", !overlong);
    check("no damaged spawn is believed that its own rule refuses", spawnsUnsound == 0);
}

/**
 * @brief The snapshot datagram @p real with its body and sequence replaced.
 *
 * The connection header and the twelve-byte message header are kept, so the
 * client takes it for the server's next word.
 *
 * @param real     A snapshot datagram the server really sent.
 * @param sequence Connection sequence the forgery carries.
 * @param body     Snapshot body to put after the kept headers.
 * @return The forged datagram.
 */
std::vector<uint8_t> reframeSnapshot(
    const std::vector<uint8_t>& real,
    uint16_t sequence,
    const std::vector<uint8_t>& body
) {
    constexpr size_t MESSAGE_HEADER = 12;
    std::vector<uint8_t> forged(
        real.begin(),
        real.begin() + static_cast<long>(NetConnection::HEADER_BYTES + MESSAGE_HEADER)
    );
    forged[0] = static_cast<uint8_t>(sequence & 0xFF);
    forged[1] = static_cast<uint8_t>(sequence >> 8);
    forged.insert(forged.end(), body.begin(), body.end());
    return forged;
}

// A snapshot writes into the client's own world: a slot it names gone is destroyed,
// the client's own included. Damaged, it may say anything a server could, and the
// client goes on - a hostile server is one this end leaves, not one that crashes it.
void testDamagedSnapshotsNeverStopAClient() {
    std::printf("Snapshots damaged %d ways, and one naming the player's own entity gone:\n", ITERATIONS);

    const ScopedEngineSchema wire;
    ResourceManager resources;

    Scene serverWorld;
    const std::vector<EntityId> crates = buildCrates(serverWorld, 6);
    NetSession server;
    server.onSpawn(
        [](Scene& scene, ResourceManager&, PlayerId) {
            const EntityId player = scene.createEntity();
            scene.add(player, Transform{});
            scene.add(player, Rigidbody{});
            return player;
        },
        [](Scene& scene, ResourceManager&, PlayerId, EntityId player) {
            scene.destroyEntity(player);
        }
    );
    server.host(0, 2, TEST_TICK_RATE);

    DelayedLink link(server.localAddress().port, 0);
    check("the link opens", link.isOpen());

    Scene clientWorld;
    NetSession client;
    client.connect(link.front(), TEST_TICK_RATE);

    uint32_t tick = 0;
    const auto frame = [&]() {
        server.receive(serverWorld, resources);
        client.receive(clientWorld, resources);
        ++tick;
        server.beginTick(tick, InputCommand{}, 3);
        client.beginTick(tick, InputCommand{}, 3);
        server.endTick(serverWorld, tick);
        client.endTick(clientWorld, tick);
        server.send(serverWorld, tick);
        client.send(clientWorld, tick);
        link.pump();
        client.interpolate(clientWorld, 1.0f / 60.0f);
        server.advance(1.0f / 60.0f);
        client.advance(1.0f / 60.0f);
    };
    check("the client joins", pumpUntil(frame, [&]() { return client.isPlaying(); }, 80));
    check(
        "and the world arrives",
        pumpUntil(frame, [&]() { return clientWorld.isAliveAtIndex(crates.back().slot()); }, 80)
    );
    for (int i = 0; i < 10; ++i) frame();

    const std::vector<uint8_t> real = link.lastToClient();
    const size_t payload = NetConnection::HEADER_BYTES;
    check(
        "the newest datagram is a snapshot",
        real.size() > payload + 24 && real[payload + 3] == static_cast<uint8_t>(NetMessage::Snapshot)
    );
    const auto sequenceOf = [](const std::vector<uint8_t>& datagram) {
        return static_cast<uint16_t>(datagram[0] | (datagram[1] << 8));
    };

    // The body as the server wrote it, from the tick on: what damaged copies come from.
    const std::vector<uint8_t> body(real.begin() + static_cast<long>(payload + 12), real.end());

    // A body reporting the player's own entity gone and nothing else: prediction was
    // set aside before the read, and the entity it would reconcile is gone.
    {
        BitReader header(body.data(), body.size());
        const uint32_t serverTick = header.u32();
        const uint32_t confirmed  = header.u32();
        const uint32_t heard      = header.u32();
        const uint32_t lowWater   = header.bits(NET_QUEUE_DEPTH_BITS);

        std::vector<uint8_t> lost;
        BitWriter writer(lost, 64);
        writer.u32(serverTick + 1);
        writer.u32(confirmed);
        writer.u32(heard);
        writer.bits(lowWater, NET_QUEUE_DEPTH_BITS);
        writer.bits(1, 8);
        writer.bits(client.localEntity().slot(), 24);
        writer.boolean(false);
        writer.finish();
        link.forgeToClient(reframeSnapshot(real, static_cast<uint16_t>(sequenceOf(real) + 1), lost));
    }
    client.receive(clientWorld, resources);
    check("its own entity reported gone, the client plays on", client.isPlaying());
    for (int i = 0; i < 5; ++i) frame();

    Math::Rng rng(SEED);
    for (int i = 0; i < ITERATIONS; ++i) {
        const std::vector<uint8_t>& newest = link.lastToClient();
        link.forgeToClient(
            reframeSnapshot(newest, static_cast<uint16_t>(sequenceOf(newest) + 1), damage(body, rng))
        );
        frame();
    }
    check("nor do damaged ones: it is still playing", client.isPlaying());

    // Still listening: a body the server moves is drawn where it went.
    const EntityId crate = crates.front();
    serverWorld.get<Transform>(crate).position.x = -40.0f;
    const auto arrived = [&]() {
        if (!clientWorld.isAliveAtIndex(crate.slot())) return false;
        const Transform* at = clientWorld.tryGet<Transform>(clientWorld.entityAt(crate.slot()));
        return at && std::abs(at->position.x + 40.0f) < 0.01f;
    };
    check("  and what the server moves afterwards still arrives", pumpUntil(frame, arrived, 120));

    client.close();
    server.close();
}

/**
 * @brief A NetSpawn's bytes as a hostile server would write them, followed by a marker.
 *
 * Forged by hand, because the encoder will not write what isSayable refuses.
 *
 * @param path   Prefab path, written with an 8-bit length.
 * @param scale  Written to all three scale components.
 * @param marker Written after the spawn, for the test to read back.
 * @return The encoded bytes.
 */
std::vector<uint8_t> forgeSpawn(const std::string& path, float scale, uint32_t marker) {
    std::vector<uint8_t> bytes;
    BitWriter writer(bytes, 512);
    writer.bits(static_cast<uint32_t>(path.size()), 8);
    for (const char c : path) writer.bits(static_cast<uint8_t>(c), 8);
    for (int i = 0; i < 3; ++i) writer.f32(1.0f);
    writer.f32(1.0f);
    for (int i = 0; i < 3; ++i) writer.f32(0.0f);
    for (int i = 0; i < 3; ++i) writer.f32(scale);
    writer.bits(0, 6);   // no slots
    writer.u32(marker);
    writer.finish();
    return bytes;
}

// A spawn's path is a file the client opens on the server's word. Outside the
// project it is the server reading the client's disk, or on Windows, via a share,
// collecting the login hash; a FIFO or device blocks the frame for good. A scale the
// client clamps is a body the server built at a size the client never will.
void testASpawnCannotNameAFileOutsideTheProject() {
    std::printf("Spawns naming what a client must never open:\n");

    constexpr uint32_t MARKER = 0xC0FFEEu;
    const auto believed = [&](const std::string& path, float scale) {
        const std::vector<uint8_t> bytes = forgeSpawn(path, scale, MARKER);
        BitReader reader(bytes.data(), bytes.size());
        NetSpawn read;
        netDecode(read, reader);
        const bool inStep = reader.u32() == MARKER && !reader.failed();
        return inStep && !read.prefab.empty();
    };

    check("an ordinary project prefab is believed", believed("prefabs/crate.json", 1.0f));
    check("  as is one in a folder of its own", believed("prefabs/props/barrel.v2.json", 1.0f));

    const char* const hostile[] = {
        "/etc/passwd.json",            // absolute, POSIX
        "\\\\attacker\\share\\x.json", // a share, as Windows reads it
        "//attacker/share/x.json",     // the same share, spelt forwards
        "C:/Users/player/x.json",      // a drive
        "C:x.json",                    // relative to a drive's own directory
        "prefabs/x.json:s.json",       // an alternate data stream
        "prefabs\\..\\..\\x.json",     // a parent, spelt backwards
        "../outside.json",             // a parent
        "prefabs/../../outside.json",  // a parent past the project root
        "prefabs/../x.json",           // a parent, even one that stays inside
        "prefabs/con.json",            // a Windows device
        "prefabs/COM1.json",           // another, in capitals
        "prefabs/fifo",                // not a prefab file
        "prefabs/x.json.exe",          // nor this
        "prefabs/x\x01.json",          // a control character
    };
    size_t refused = 0;
    for (const char* path : hostile) {
        if (believed(path, 1.0f)) {
            std::printf("      believed: %s\n", path);
        } else {
            ++refused;
        }
    }
    check(
        "every path that leaves the project, or is not a prefab file, is refused",
        refused == std::size(hostile)
    );

    const std::string truncated = std::string("prefabs/x") + '\0' + ".json";
    check("  as is one a NUL would cut short of its extension", !believed(truncated, 1.0f));

    check(
        "a scale past what the wire carries is refused rather than clamped",
        !believed("prefabs/crate.json", 2.0f * NET_MAX_SCALE)
    );
    check("  and the largest it carries is believed", believed("prefabs/crate.json", NET_MAX_SCALE));
}

} // namespace

void runHostileTests() {
    testDamagedCookedFilesAreRefusedOrSound();
    testADamagedSceneLoadsOrIsRefused();
    testDamagedDatagramsAreRefusedOrBounded();
    testDamagedSnapshotsNeverStopAClient();
    testASpawnCannotNameAFileOutsideTheProject();
}
