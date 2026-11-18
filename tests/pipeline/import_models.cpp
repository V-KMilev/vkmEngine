// Model files imported as the editor imports them: glTF, GLB, OBJ and FBX each landing on the
// engine's axes in meters, a rig and its clip with them, and a damaged file refused by name.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

#include <glm/gtc/type_precision.hpp>

#include "ecs/component/animation/animator.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/component/scene/transform.h"
#include "ecs/scene.h"
#include "import/model/model_import.h"
#include "import/model/source_model.h"
#include "resource/resource_manager.h"
#include "system/async_loader/async_loader_system.h"

#include "harness/check.h"
#include "harness/world.h"
#include "pipeline/art.h"

using namespace Vkm::Test;

namespace {

// Not a cube: one meter across, two up, half deep, standing on the ground, so an up axis or a
// unit the import got wrong shows in its bounds.
const glm::vec3 LOW(-0.5f, 0.0f, -0.25f);
const glm::vec3 HIGH(0.5f, 2.0f, 0.25f);

/**
 * @brief Every corner as (position, normal, uv), rounded and sorted, to compare two imports by.
 */
using Corner = std::tuple<long, long, long, long, long, long, long, long>;
std::vector<Corner> cornersOf(const MeshAsset& mesh) {
    const auto r = [](float v) { return std::lround(v * 1000.0f); };
    std::vector<Corner> corners;
    for (const uint32_t index : mesh.indices) {
        const Vertex& v = mesh.vertices[index];
        corners.emplace_back(
            r(v.position.x),
            r(v.position.y),
            r(v.position.z),
            r(v.normal.x),
            r(v.normal.y),
            r(v.normal.z),
            r(v.uv.x),
            r(v.uv.y)
        );
    }
    std::sort(corners.begin(), corners.end());
    return corners;
}

/// What the faces say each corner should be, the same way.
std::vector<Corner> cornersOf(const std::vector<Face>& faces) {
    MeshAsset mesh;
    for (const Face& face : faces) {
        for (const int k : {0, 1, 2, 0, 2, 3}) {
            mesh.indices.push_back(static_cast<uint32_t>(mesh.vertices.size()));
            mesh.vertices.push_back({face.corners[k], face.normal, face.uvs[k], glm::vec4(0.0f)});
        }
    }
    return cornersOf(mesh);
}

float volumeOf(const MeshAsset& mesh) {
    double volume = 0.0;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const glm::vec3& a = mesh.vertices[mesh.indices[i]].position;
        const glm::vec3& b = mesh.vertices[mesh.indices[i + 1]].position;
        const glm::vec3& c = mesh.vertices[mesh.indices[i + 2]].position;
        volume += glm::dot(a, glm::cross(b, c));
    }
    return static_cast<float>(volume / 6.0);
}

/// The entity that draws @p mesh, or none.
EntityId drawing(Scene& scene, MeshHandle mesh) {
    EntityId found{};
    scene.forEach<Mesh>([&](EntityId id, const Mesh& drawn) {
        if (drawn.mesh == mesh) found = id;
    });
    return found;
}

EntityId parentOf(const Scene& scene, EntityId id) {
    return scene.has<Hierarchy>(id) ? scene.get<Hierarchy>(id).parent : EntityId{};
}

/// A box model's mesh, imported into @p scene from @p ref under the project.
MeshHandle importBox(const std::string& ref, ResourceManager& resources, Scene& scene) {
    const ModelImport result = importModelIntoScene(ref, resources, scene);
    REQUIRE(result.ok);
    REQUIRE(awaitAsyncLoads(resources));
    const MeshHandle mesh = resources.findByName<MeshAsset>(ref + ":mesh0");
    REQUIRE(mesh);
    return mesh;
}

} // namespace

TEST("a glTF imports as its mesh in meters on the engine's axes, its V turned up and its parts named by the file") {
    const auto root = openProject();
    Gltf gltf;
    const int mesh = addMesh(gltf, boxFaces(LOW, HIGH));
    gltf.add("nodes", {{"name", "stand"}, {"children", {1}}});
    gltf.add("nodes", {{"name", "box"}, {"mesh", mesh}, {"translation", {0.0, 0.0, 3.0}}});
    gltf.add("scenes", {{"nodes", {0}}});
    writeText(root / "assets/box.gltf", gltf.text());

    ResourceManager resources;
    Scene scene;
    const MeshHandle box = importBox("assets/box.gltf", resources, scene);
    const MeshAsset& m   = resources.get(box);
    CHECK(m.vertices.size() == 24);
    CHECK(m.indices.size() == 36);
    CHECK_NEAR(m.boundsMin, LOW, 1e-5f);
    CHECK_NEAR(m.boundsMax, HIGH, 1e-5f);
    CHECK_NEAR(volumeOf(m), 1.0f, 1e-4f);
    CHECK(cornersOf(m) == cornersOf(boxFaces(LOW, HIGH)));
    // The file carries no tangents: each comes out along +U, at right angles to its normal.
    for (const Vertex& v : m.vertices) {
        CHECK_NEAR(glm::dot(glm::vec3(v.tangent), v.normal), 0.0f, 1e-4f);
        CHECK_NEAR(glm::length(glm::vec3(v.tangent)), 1.0f, 1e-4f);
        if (v.normal.z > 0.5f) CHECK_NEAR(glm::vec3(v.tangent), glm::vec3(1.0f, 0.0f, 0.0f), 1e-4f);
    }

    const EntityId placed = drawing(scene, box);
    REQUIRE(placed);
    CHECK_NEAR(scene.get<Transform>(placed).position, glm::vec3(0.0f, 0.0f, 3.0f), 1e-6f);
    CHECK(resources.isAlive(scene.get<Mesh>(placed).material));

    // Imported again, every part is the one already there: a scene naming them keeps its links.
    const size_t meshes    = resources.countOfType<MeshAsset>();
    const size_t materials = resources.countOfType<MaterialAsset>();
    Scene again;
    CHECK(importBox("assets/box.gltf", resources, again) == box);
    CHECK(resources.countOfType<MeshAsset>() == meshes);
    CHECK(resources.countOfType<MaterialAsset>() == materials);
    CHECK(scene.get<Mesh>(placed).material == again.get<Mesh>(drawing(again, box)).material);
}

TEST("a GLB's embedded image imports as its material's texture") {
    const auto root = openProject();
    const std::vector<uint8_t> pixels = {255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 255, 40, 80, 120, 0};
    const std::string png = pngBytes(2, 2, 4, pixels);

    Gltf gltf;
    const int view  = gltf.view(png.data(), png.size());
    const int image = gltf.add("images", {{"bufferView", view}, {"mimeType", "image/png"}});
    gltf.add("textures", {{"source", image}});
    const int material = gltf.add("materials", {
        {"name", "painted"},
        {"pbrMetallicRoughness", {{"baseColorTexture", {{"index", 0}}},
                                  {"baseColorFactor", {1.0, 0.5, 0.25, 1.0}},
                                  {"metallicFactor", 0.0},
                                  {"roughnessFactor", 0.6}}},
    });
    gltf.add("nodes", {{"mesh", addMesh(gltf, boxFaces(LOW, HIGH), material)}});
    gltf.add("scenes", {{"nodes", {0}}});
    writeText(root / "assets/painted.glb", gltf.binary());

    ResourceManager resources;
    Scene scene;
    const MeshHandle box = importBox("assets/painted.glb", resources, scene);
    const MaterialHandle painted = scene.get<Mesh>(drawing(scene, box)).material;
    REQUIRE(painted);
    const MaterialAsset& m = resources.get(painted);
    CHECK(m.name() == "assets/painted.glb:mat0");
    CHECK_NEAR(m.albedo, glm::vec4(1.0f, 0.5f, 0.25f, 1.0f), 1e-6f);
    CHECK_NEAR(m.roughness, 0.6f, 1e-6f);
    REQUIRE(resources.isAlive(m.albedoTexture));
    const TextureAsset& texture = resources.get(m.albedoTexture);
    CHECK(texture.name() == "assets/painted.glb:emb:*0#Color");
    CHECK(texture.isSrgb());
    CHECK(texture.params.width == 2);
    CHECK(texture.params.height == 2);
    // Stored from the bottom row up, as every texture is.
    const std::vector<uint8_t> bottomUp = {0, 0, 255, 255, 40, 80, 120, 0, 255, 0, 0, 255, 0, 255, 0, 128};
    CHECK(texture.pixelData == bottomUp);

    // Its recipe opens the model again for the image.
    ResourceManager other;
    const TextureHandle again =
        loadModelEmbeddedTexture("assets/painted.glb", "*0", TextureUsage::Color, other);
    REQUIRE(again);
    CHECK(other.get(again).name() == texture.name());
    CHECK(other.get(again).pixelData == texture.pixelData);
}

TEST("an OBJ in meters and an FBX in centimeters with Z up import the box the glTF does") {
    const auto root = openProject();
    const std::vector<Face> faces = boxFaces(LOW, HIGH);
    writeText(root / "assets/box.obj", objText(faces));
    writeText(root / "assets/box.fbx", fbxText(faces));

    for (const char* ref : {"assets/box.obj", "assets/box.fbx"}) {
        std::printf("  %s\n", ref);
        ResourceManager resources;
        Scene scene;
        const MeshHandle box = importBox(ref, resources, scene);
        CHECK(resources.get(box).vertices.size() == 24);

        // As it stands in the world: a turn of axes may sit on a node, a change of unit may not.
        MeshAsset world = resources.get(box);
        for (EntityId at = drawing(scene, box); at; at = parentOf(scene, at)) {
            const Transform& pose = scene.get<Transform>(at);
            CHECK_NEAR(pose.scale, glm::vec3(1.0f), 1e-5f);
            for (Vertex& v : world.vertices) {
                v.position = pose.position + pose.rotation * (pose.scale * v.position);
                v.normal   = pose.rotation * v.normal;
            }
        }
        world.computeAndSetBounds();
        CHECK_NEAR(world.boundsMin, LOW, 1e-4f);
        CHECK_NEAR(world.boundsMax, HIGH, 1e-4f);
        CHECK_NEAR(volumeOf(world), 1.0f, 1e-3f);
        CHECK(cornersOf(world) == cornersOf(faces));
    }
}

TEST("a skinned glTF imports its rig, its clip and a mesh bound to them") {
    const auto root = openProject();
    writeText(root / "assets/rig.gltf", riggedGltf());
    const float half = std::sqrt(0.5f);

    ResourceManager resources;
    Scene scene;
    const MeshHandle body = importBox("assets/rig.gltf", resources, scene);

    const SkeletonHandle rig = resources.findByName<SkeletonAsset>("assets/rig.gltf:skeleton");
    REQUIRE(rig);
    const SkeletonAsset& s = resources.get(rig);
    REQUIRE(s.bones.size() == 2);
    CHECK(s.bones[0].name == "hip");
    CHECK(s.bones[0].parent == -1);
    CHECK(s.bones[1].name == "spine");
    CHECK(s.bones[1].parent == 0);
    CHECK(findSkeletonFault(s).empty());
    CHECK_NEAR(s.inverseBind[1][3], glm::vec4(0.0f, -1.0f, 0.0f, 1.0f), 1e-6f);
    CHECK_NEAR(s.bindPose[1].position, glm::vec3(0.0f, 1.0f, 0.0f), 1e-6f);

    const AnimationClipHandle sway = resources.findByName<AnimationClipAsset>("assets/rig.gltf:clip0");
    REQUIRE(sway);
    const AnimationClipAsset& c = resources.get(sway);
    CHECK(findClipFault(c).empty());
    CHECK(c.skeleton == s.name());
    CHECK_NEAR(c.duration, 1.0f, 1e-6f);
    REQUIRE(c.bones.size() == 2);
    REQUIRE(c.bones[1].rotation.count == 2);
    CHECK_NEAR(c.rotationTimes[c.bones[1].rotation.first + 1], 1.0f, 1e-6f);
    CHECK_NEAR(c.rotations[c.bones[1].rotation.first + 1], glm::quat(half, 0.0f, 0.0f, half), 1e-5f);
    REQUIRE(c.bones[0].position.count == 3);
    CHECK_NEAR(c.positions[c.bones[0].position.first + 1], glm::vec3(0.0f, 0.5f, 0.0f), 1e-6f);
    CHECK(c.bones[0].rotation.count == 0);

    const MeshAsset& m = resources.get(body);
    CHECK(m.skeleton == s.name());
    REQUIRE(m.skin.size() == m.vertices.size());
    CHECK(findMeshFault(m).empty());
    for (size_t i = 0; i < m.skin.size(); ++i) {
        const SkinVertex& skin = m.skin[i];
        CHECK(skin.weights[0] + skin.weights[1] + skin.weights[2] + skin.weights[3] == 255);
        if (m.vertices[i].position.y > 1.0f) {
            CHECK(skin.bones[0] == 1);
            CHECK(skin.weights[0] == 191);
            CHECK(skin.bones[1] == 0);
        } else {
            CHECK(skin.bones[0] == 0);
            CHECK(skin.weights[0] == 255);
        }
    }
    // The farthest a bone's vertex sits from it: a top corner from the hip.
    CHECK_NEAR(m.skinRadius, glm::length(glm::vec3(0.25f, 2.0f, 0.25f)), 1e-4f);

    int animators = 0;
    scene.forEach<Animator>([&](EntityId, const Animator& animator) {
        ++animators;
        CHECK(animator.skeleton == rig);
    });
    CHECK(animators == 1);
}

TEST("influences tied in weight, over two sets, put the lower bone first on every platform") {
    const auto root = openProject();
    Gltf gltf;
    const std::vector<Face> faces = boxFaces(LOW, HIGH);
    const size_t corners = faces.size() * 4;
    // Every weight a quarter, the spine listed first in one set and second in the other, so only the
    // rule for a tie decides which bones lead.
    const std::vector<glm::u8vec4> spineFirst(corners, glm::u8vec4(1, 0, 0, 0));
    const std::vector<glm::u8vec4> hipFirst(corners, glm::u8vec4(0, 1, 0, 0));
    const std::vector<glm::vec4>   quarters(corners, glm::vec4(0.25f, 0.25f, 0.0f, 0.0f));
    const int mesh = addMesh(gltf, faces);
    nlohmann::json& attributes = gltf.doc["meshes"][mesh]["primitives"][0]["attributes"];
    attributes["JOINTS_0"]     = gltf.joints(spineFirst);
    attributes["WEIGHTS_0"]    = gltf.vec4s(quarters);
    attributes["JOINTS_1"]     = gltf.joints(hipFirst);
    attributes["WEIGHTS_1"]    = gltf.vec4s(quarters);
    gltf.add("nodes", {{"name", "root"}, {"children", {1, 3}}});
    gltf.add("nodes", {{"name", "hip"}, {"children", {2}}});
    gltf.add("nodes", {{"name", "spine"}, {"translation", {0.0, 1.0, 0.0}}});
    gltf.add("nodes", {{"name", "body"}, {"mesh", mesh}, {"skin", 0}});
    gltf.add("skins", {{"joints", {1, 2}}});
    gltf.add("scenes", {{"nodes", {0}}});
    writeText(root / "assets/tied.gltf", gltf.text());

    ResourceManager resources;
    Scene scene;
    const MeshAsset& m = resources.get(importBox("assets/tied.gltf", resources, scene));
    REQUIRE(m.skin.size() == m.vertices.size());
    // Each quarter rounds to 64 of 255; the one over comes off the first.
    const SkinVertex tied = {{0, 0, 1, 1}, {63, 64, 64, 64}};
    for (const SkinVertex& skin : m.skin) CHECK(std::memcmp(&skin, &tied, sizeof(SkinVertex)) == 0);
}

TEST("a damaged or missing model file is refused, naming it, and nothing of it is added") {
    const auto root = openProject();
    Gltf gltf;
    gltf.add("nodes", {{"mesh", addMesh(gltf, boxFaces(LOW, HIGH))}});
    gltf.add("scenes", {{"nodes", {0}}});
    const std::string glb = gltf.binary();
    writeText(root / "assets/cut.glb", glb.substr(0, glb.size() / 2));
    writeText(root / "assets/garbled.gltf", "{ \"asset\": { \"version\": \"2.0\" }, \"meshes\": [ {");
    Gltf past = gltf;
    past.doc["accessors"][0]["count"] = 100000;  // positions far past the buffer
    writeText(root / "assets/past.gltf", past.text());
    const std::string binaryFbx = std::string("Kaydara FBX Binary  \0\x1a\0", 23);
    writeText(root / "assets/garbled.fbx", binaryFbx + std::string(64, '\x7f'));

    const char* const refs[] = {
        "assets/missing.gltf",
        "assets/cut.glb",
        "assets/garbled.gltf",
        "assets/past.gltf",
        "assets/garbled.fbx",
    };
    for (const char* ref : refs) {
        std::printf("  %s\n", ref);
        ResourceManager resources;
        Scene scene;
        const int said = countInLog(std::string("'") + ref + "'");
        CHECK(!importModelIntoScene(ref, resources, scene).ok);
        CHECK(countInLog(std::string("'") + ref + "'") > said);
        CHECK(resources.countOfType<MeshAsset>() == 0);
        CHECK(resources.countOfType<MaterialAsset>() == 0);
        CHECK(scene.entityCount() == 0);
        // Asked for as a part, it comes out empty, said.
        const bool asked = static_cast<bool>(requestModelMeshAsync(ref, 0, resources));
        CHECK((!asked || !awaitAsyncLoads(resources)));
    }
}
