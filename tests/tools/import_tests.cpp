#include "support.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "import/model_part_names.h"
#include "import/model_source.h"

namespace {

// What an import makes of a model file, read through parseModel from the smallest
// file for each case, written here.

void write(const std::filesystem::path& path, const void* bytes, size_t size) {
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(size));
}

void write(const std::filesystem::path& path, const std::string& text) {
    write(path, text.data(), text.size());
}

// The share of corners whose bitangent, cross(normal, tangent) * w, runs the way V
// grows - the way a baked normal map's green channel and the PBR shader point.
float bitangentsAlongV(const MeshAsset& mesh) {
    size_t along = 0;
    size_t corners = 0;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vertex& a = mesh.vertices[mesh.indices[i]];
        const Vertex& b = mesh.vertices[mesh.indices[i + 1]];
        const Vertex& c = mesh.vertices[mesh.indices[i + 2]];
        const glm::vec3 e1 = b.position - a.position;
        const glm::vec3 e2 = c.position - a.position;
        const glm::vec2 d1 = b.uv - a.uv;
        const glm::vec2 d2 = c.uv - a.uv;
        const float det = d1.x * d2.y - d2.x * d1.y;
        if (std::abs(det) <= glm::epsilon<float>()) continue;
        const glm::vec3 alongV = (e2 * d1.x - e1 * d2.x) / det;
        for (const Vertex* v : {&a, &b, &c}) {
            const glm::vec3 bitangent = glm::cross(v->normal, glm::vec3(v->tangent)) * v->tangent.w;
            ++corners;
            if (glm::dot(bitangent, alongV) > 0.0f) ++along;
        }
    }
    return corners ? static_cast<float>(along) / static_cast<float>(corners) : 0.0f;
}

// A quad facing +Z, @p side on a side, with glTF's V down the image: UV (0,0) sits
// at the top of the quad. Vertices go in quad.bin beside it, as an exporter writes
// them, so the .gltf text is the same whatever the side.
void writeGltfQuad(const std::filesystem::path& dir, float side) {
    const float positions[] = {0, 0, 0,  side, 0, 0,  side, side, 0,  0, side, 0};
    const float normals[]   = {0, 0, 1,  0, 0, 1,  0, 0, 1,  0, 0, 1};
    const float uvs[]       = {0, 1,  1, 1,  1, 0,  0, 0};
    const uint16_t indices[] = {0, 1, 2, 0, 2, 3};
    std::vector<unsigned char> bin;
    const std::initializer_list<std::pair<const void*, size_t>> parts = {
        {positions, sizeof(positions)},
        {normals, sizeof(normals)},
        {uvs, sizeof(uvs)},
        {indices, sizeof(indices)}
    };
    for (const auto& [data, size] : parts) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        bin.insert(bin.end(), bytes, bytes + size);
    }
    write(dir / "quad.bin", bin.data(), bin.size());
    const char* gltf = R"({
        "asset": {"version": "2.0"},
        "buffers": [{"uri": "quad.bin", "byteLength": 140}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0,   "byteLength": 48},
            {"buffer": 0, "byteOffset": 48,  "byteLength": 48},
            {"buffer": 0, "byteOffset": 96,  "byteLength": 32},
            {"buffer": 0, "byteOffset": 128, "byteLength": 12}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                    "indices": 3}]}],
        "nodes": [{"mesh": 0}],
        "scenes": [{"nodes": [0]}]
    })";
    write(dir / "quad.gltf", gltf);
}

void testAGltfQuadArrivesWithItsMapsUpright() {
    std::printf("A glTF quad, as the import reads it:\n");
    const ScratchProject project("vkm_import_gltf");
    writeGltfQuad(project.root(), 1.0f);

    const std::unique_ptr<SourceModel> model =
        parseModel((project.root() / "quad.gltf").string(), "quad.gltf");
    check("it parses", model != nullptr);
    if (!model) return;
    check(
        "  into one mesh of two triangles",
        model->meshes.size() == 1 && model->meshes[0].geometry.indices.size() == 6
    );
    if (model->meshes.size() != 1) return;
    const MeshAsset& quad = model->meshes[0].geometry;

    // The engine's V runs up the image, so the quad's top reads V = 1.
    bool flipped = !quad.vertices.empty();
    for (const Vertex& v : quad.vertices) flipped = flipped && std::abs(v.uv.y - v.position.y) < 1e-6f;
    check("  its V turned to run up the image, as the engine's does", flipped);
    check(
        "  and every tangent along +U",
        !quad.vertices.empty()
            && std::all_of(
                quad.vertices.begin(),
                quad.vertices.end(),
                [](const Vertex& v) { return glm::dot(glm::vec3(v.tangent), {1, 0, 0}) > 0.99f; }
            )
    );
    check(
        "  with the bitangent the way V grows, where a baked map's green points",
        bitangentsAlongV(quad) == 1.0f
    );
}

// cgltf returns zeros for data it does not decode, which would cook as the model:
// such a file is refused, and says why.
void testAGltfThisImporterCannotDecodeIsRefused() {
    std::printf("A glTF whose data this importer cannot decode:\n");
    const ScratchProject project("vkm_import_undecoded");
    writeGltfQuad(project.root(), 1.0f);
    const std::filesystem::path path = project.root() / "quad.gltf";

    std::string text;
    {
        std::ifstream in(path);
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const auto parsesAs = [&](const std::string& from, const std::string& to) {
        std::string edited = text;
        const size_t at = edited.find(from);
        if (at == std::string::npos) return true;
        edited.replace(at, from.size(), to);
        write(path, edited);
        return parseModel(path.string(), "quad.gltf") != nullptr;
    };

    check("the quad as written parses", parsesAs("", ""));
    check(
        "positions held in a sparse accessor are refused",
        !parsesAs(
            R"("count": 4, "type": "VEC3"},)",
            R"("count": 4, "type": "VEC3", "sparse": {"count": 1,)"
            R"( "indices": {"bufferView": 3, "componentType": 5123}, "values": {"bufferView": 0}}},)"
        )
    );
    check(
        "a file that requires a compression extension is refused",
        !parsesAs(
            R"("asset": {"version": "2.0"},)",
            R"("asset": {"version": "2.0"}, "extensionsUsed": ["EXT_meshopt_compression"],)"
            R"( "extensionsRequired": ["EXT_meshopt_compression"],)"
        )
    );
}

// Two files with one stem in two folders are two models: parts are named by the
// file's reference, so the by-name dedup cannot hand one the other's mesh.
void testTwoModelsWithOneStemAreTwoModels() {
    std::printf("Two quad.gltf files in two folders:\n");
    using namespace ModelPartNames;
    check("their meshes have two names", meshName("near/quad.gltf", 0) != meshName("far/quad.gltf", 0));
    check("  as do a glb and an fbx beside it", skeletonName("crate.glb") != skeletonName("crate.fbx"));
    check("  each the reference and the part", clipName("assets/run.fbx", 2) == "assets/run.fbx:clip2");
}

// A parse is reused only while the file is the one parsed. Keyed by path alone, a
// model re-exported mid-session would keep its first parse, filed under a key
// folded from the new bytes - where nothing would ever re-bake it.
void testAReExportedModelIsParsedAgain() {
    std::printf("A model parsed once and then re-exported:\n");
    const ScratchProject project("vkm_import_reexport");
    const std::string path = (project.root() / "quad.gltf").string();

    const auto widest = [](const std::shared_ptr<const SourceModel>& model) {
        float x = 0.0f;
        if (!model || model->meshes.empty()) return x;
        for (const Vertex& v : model->meshes[0].geometry.vertices) x = std::max(x, v.position.x);
        return x;
    };

    writeGltfQuad(project.root(), 1.0f);
    const std::shared_ptr<const SourceModel> first = loadSourceModel(path, "quad.gltf");
    check("the first export parses", widest(first) == 1.0f);
    check("  and asking again is the same parse", loadSourceModel(path, "quad.gltf") == first);

    // The .gltf stays byte for byte the same; only the buffer moves, same size and
    // a whole second later, so the case does not lean on filesystem time resolution.
    const auto before = std::filesystem::last_write_time(project.root() / "quad.bin");
    writeGltfQuad(project.root(), 2.0f);
    std::filesystem::last_write_time(project.root() / "quad.bin", before + std::chrono::seconds(1));
    check(
        "re-exporting its buffer alone is parsed again",
        widest(loadSourceModel(path, "quad.gltf")) == 2.0f
    );
}

// The same quad as an OBJ, which ufbx reads: V already runs up the image.
void testAnObjQuadArrivesWithItsMapsUpright() {
    std::printf("An OBJ quad, as the import reads it:\n");
    const ScratchProject project("vkm_import_obj");
    write(
        project.root() / "quad.obj",
        "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
        "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
        "vn 0 0 1\n"
        "f 1/1/1 2/2/1 3/3/1 4/4/1\n"
    );

    const std::unique_ptr<SourceModel> model =
        parseModel((project.root() / "quad.obj").string(), "quad.obj");
    check("it parses", model != nullptr);
    if (!model) return;
    check(
        "  into one mesh of two triangles",
        model->meshes.size() == 1 && model->meshes[0].geometry.indices.size() == 6
    );
    if (model->meshes.size() != 1) return;
    check("  with the bitangent the way V grows", bitangentsAlongV(model->meshes[0].geometry) == 1.0f);
}

// An FBX in centimetres, as Mixamo's are, with the empty take most exporters leave.
void testACentimetreFbxArrivesInMetres() {
    std::printf("An FBX in centimetres, as the import reads it:\n");
    const ScratchProject project("vkm_import_fbx");
    write(
        project.root() / "quad.fbx",
        R"(; FBX 7.4.0 project file
FBXHeaderExtension:  {
    FBXHeaderVersion: 1003
    FBXVersion: 7400
}
GlobalSettings:  {
    Version: 1000
    Properties70:  {
        P: "UpAxis", "int", "Integer", "",1
        P: "UpAxisSign", "int", "Integer", "",1
        P: "FrontAxis", "int", "Integer", "",2
        P: "FrontAxisSign", "int", "Integer", "",1
        P: "CoordAxis", "int", "Integer", "",0
        P: "CoordAxisSign", "int", "Integer", "",1
        P: "UnitScaleFactor", "double", "Number", "",1
    }
}
Objects:  {
    Geometry: 1000, "Geometry::quad", "Mesh" {
        Vertices: *12 {
            a: 0,0,0,100,0,0,100,100,0,0,100,0
        }
        PolygonVertexIndex: *4 {
            a: 0,1,2,-4
        }
        GeometryVersion: 124
        LayerElementNormal: 0 {
            Version: 101
            Name: ""
            MappingInformationType: "ByPolygonVertex"
            ReferenceInformationType: "Direct"
            Normals: *12 {
                a: 0,0,1,0,0,1,0,0,1,0,0,1
            }
        }
        LayerElementUV: 0 {
            Version: 101
            Name: "map1"
            MappingInformationType: "ByPolygonVertex"
            ReferenceInformationType: "IndexToDirect"
            UV: *8 {
                a: 0,0,1,0,1,1,0,1
            }
            UVIndex: *4 {
                a: 0,1,2,3
            }
        }
        Layer: 0 {
            Version: 100
            LayerElement:  {
                Type: "LayerElementNormal"
                TypedIndex: 0
            }
            LayerElement:  {
                Type: "LayerElementUV"
                TypedIndex: 0
            }
        }
    }
    Model: 2000, "Model::quad", "Mesh" {
        Version: 232
    }
    AnimationStack: 3000, "AnimStack::Take 001", "" {
    }
    AnimationLayer: 3001, "AnimLayer::BaseLayer", "" {
    }
}
Connections:  {
    C: "OO",2000,0
    C: "OO",1000,2000
    C: "OO",3001,3000
}
)"
    );

    const std::unique_ptr<SourceModel> model =
        parseModel((project.root() / "quad.fbx").string(), "quad.fbx");
    check("it parses", model != nullptr);
    if (!model) return;
    check(
        "  into one mesh of two triangles",
        model->meshes.size() == 1 && model->meshes[0].geometry.indices.size() == 6
    );
    if (model->meshes.size() != 1) return;
    MeshAsset quad = model->meshes[0].geometry;
    quad.computeAndSetBounds();
    check(
        "  a metre across, not a hundred: the engine's unit is the metre",
        std::abs(quad.boundsMax.x - 1.0f) < 1e-5f && std::abs(quad.boundsMax.y - 1.0f) < 1e-5f
    );
    bool unitScale = true;
    for (const SourceNode& node : model->nodes) {
        unitScale = unitScale && glm::all(glm::epsilonEqual(node.local.scale, glm::vec3(1.0f), 1e-6f));
    }
    check("  with the conversion in the geometry, leaving every node at unit scale", unitScale);
    check("  the bitangent the way V grows", bitangentsAlongV(quad) == 1.0f);
    check(
        "  and no clip for a take that animates nothing, which would renumber every clip after it",
        model->clips.empty()
    );
}

// A skinned FBX whose mesh precedes its joint in file order: a skin names joints
// anywhere in the tree, so the joint must be found wherever the walk reaches it.
void testAnFbxSkinFindsAJointTheWalkReachesLater() {
    std::printf("An FBX skin naming a joint after its mesh:\n");
    const ScratchProject project("vkm_import_fbx_skin");
    write(
        project.root() / "skinned.fbx",
        R"(; FBX 7.4.0 project file
FBXHeaderExtension:  {
    FBXHeaderVersion: 1003
    FBXVersion: 7400
}
Objects:  {
    Geometry: 1000, "Geometry::tri", "Mesh" {
        Vertices: *9 {
            a: 0,0,0,1,0,0,0,1,0
        }
        PolygonVertexIndex: *3 {
            a: 0,1,-3
        }
        GeometryVersion: 124
    }
    Model: 2000, "Model::body", "Mesh" {
        Version: 232
    }
    Model: 4000, "Model::joint", "LimbNode" {
        Version: 232
    }
    Deformer: 5000, "Deformer::skin", "Skin" {
        Version: 101
    }
    Deformer: 5001, "SubDeformer::cluster", "Cluster" {
        Version: 100
        Indexes: *3 {
            a: 0,1,2
        }
        Weights: *3 {
            a: 1,1,1
        }
        Transform: *16 {
            a: 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1
        }
        TransformLink: *16 {
            a: 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1
        }
    }
}
Connections:  {
    C: "OO",2000,0
    C: "OO",4000,0
    C: "OO",1000,2000
    C: "OO",5000,1000
    C: "OO",5001,5000
    C: "OO",4000,5001
}
)"
    );

    const std::unique_ptr<SourceModel> model =
        parseModel((project.root() / "skinned.fbx").string(), "skinned.fbx");
    check("it parses", model != nullptr);
    if (!model) return;
    check(
        "  into one skinned mesh of one joint",
        model->meshes.size() == 1 && model->meshes[0].bones.size() == 1
    );
    if (model->meshes.size() != 1 || model->meshes[0].bones.size() != 1) return;
    const uint32_t joint = model->meshes[0].bones[0].node;
    check(
        "  bound to the joint the file names, though the walk meets it after the mesh",
        joint < model->nodes.size() && model->nodes[joint].name == "joint"
    );
    const MeshAsset& tri = model->meshes[0].geometry;
    check(
        "  with every vertex wholly on it",
        tri.skin.size() == tri.vertices.size()
            && std::all_of(
                tri.skin.begin(),
                tri.skin.end(),
                [](const SkinVertex& v) { return v.bones[0] == 0 && v.weights[0] == 255; }
            )
    );
}

} // namespace

void runImportTests() {
    testAGltfQuadArrivesWithItsMapsUpright();
    testAnObjQuadArrivesWithItsMapsUpright();
    testACentimetreFbxArrivesInMetres();
    testAnFbxSkinFindsAJointTheWalkReachesLater();
    testAReExportedModelIsParsedAgain();
    testAGltfThisImporterCannotDecodeIsRefused();
    testTwoModelsWithOneStemAreTwoModels();
}
