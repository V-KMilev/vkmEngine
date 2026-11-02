#include "pipeline/art.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>
#include <tuple>

#include "harness/scratch.h"

// A private copy of the writer, as the engine's screenshot keeps its own.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "modules/stb/stb_image_write.h"
#pragma GCC diagnostic pop

namespace Vkm::Test {

namespace {

void appendTo(void* context, void* data, int size) {
    static_cast<std::string*>(context)->append(static_cast<const char*>(data), static_cast<size_t>(size));
}

std::string base64(const std::string& bytes) {
    static const char* const DIGITS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < bytes.size(); i += 3) {
        uint32_t group  = static_cast<uint8_t>(bytes[i]) << 16;
        const size_t in = std::min<size_t>(3, bytes.size() - i);
        if (in > 1) group |= static_cast<uint8_t>(bytes[i + 1]) << 8;
        if (in > 2) group |= static_cast<uint8_t>(bytes[i + 2]);
        for (size_t k = 0; k < 4; ++k) out += k <= in ? DIGITS[(group >> (18 - 6 * k)) & 63] : '=';
    }
    return out;
}

template<typename T>
void put(std::string& out, T value) {
    out.append(reinterpret_cast<const char*>(&value), sizeof(T));
}

/// Numbers as an FBX array's body, every value on one line.
template<typename Each>
std::string fbxArray(const char* key, size_t count, Each&& each) {
    std::ostringstream out;
    out << "\t\t" << key << ": *" << count << " {\n\t\t\ta: ";
    for (size_t i = 0; i < count; ++i) out << (i ? "," : "") << each(i);
    out << "\n\t\t}\n";
    return out.str();
}

} // namespace

void writeImage(
    const std::filesystem::path& path,
    int width,
    int height,
    int channels,
    const std::vector<uint8_t>& pixels
) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::string file      = path.string();
    const std::string extension = path.extension().string();
    const int stride = width * channels;
    if (extension == ".png") stbi_write_png(file.c_str(), width, height, channels, pixels.data(), stride);
    if (extension == ".jpg") stbi_write_jpg(file.c_str(), width, height, channels, pixels.data(), 100);
    if (extension == ".tga") stbi_write_tga(file.c_str(), width, height, channels, pixels.data());
    if (extension == ".bmp") stbi_write_bmp(file.c_str(), width, height, channels, pixels.data());
}

std::string pngBytes(int width, int height, int channels, const std::vector<uint8_t>& pixels) {
    std::string out;
    stbi_write_png_to_func(&appendTo, &out, width, height, channels, pixels.data(), width * channels);
    return out;
}

void writeWav(
    const std::filesystem::path& path,
    uint32_t rate,
    uint16_t channels,
    const std::vector<int16_t>& samples
) {
    const auto data = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    std::string out = "RIFF";
    put<uint32_t>(out, 36 + data);
    out += "WAVEfmt ";
    put<uint32_t>(out, 16);
    put<uint16_t>(out, 1);  // PCM
    put<uint16_t>(out, channels);
    put<uint32_t>(out, rate);
    put<uint32_t>(out, rate * channels * 2);
    put<uint16_t>(out, static_cast<uint16_t>(channels * 2));
    put<uint16_t>(out, 16);
    out += "data";
    put<uint32_t>(out, data);
    out.append(reinterpret_cast<const char*>(samples.data()), data);
    writeText(path, out);
}

std::vector<Face> boxFaces(const glm::vec3& low, const glm::vec3& high) {
    const auto at = [&](int x, int y, int z) {
        return glm::vec3(x ? high.x : low.x, y ? high.y : low.y, z ? high.z : low.z);
    };
    const glm::vec2 uv[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    std::vector<Face> faces = {
        {{at(1, 0, 1), at(1, 0, 0), at(1, 1, 0), at(1, 1, 1)}, {1, 0, 0}, {}},
        {{at(0, 0, 0), at(0, 0, 1), at(0, 1, 1), at(0, 1, 0)}, {-1, 0, 0}, {}},
        {{at(0, 1, 1), at(1, 1, 1), at(1, 1, 0), at(0, 1, 0)}, {0, 1, 0}, {}},
        {{at(0, 0, 0), at(1, 0, 0), at(1, 0, 1), at(0, 0, 1)}, {0, -1, 0}, {}},
        {{at(0, 0, 1), at(1, 0, 1), at(1, 1, 1), at(0, 1, 1)}, {0, 0, 1}, {}},
        {{at(1, 0, 0), at(0, 0, 0), at(0, 1, 0), at(1, 1, 0)}, {0, 0, -1}, {}},
    };
    for (Face& face : faces) {
        for (int k = 0; k < 4; ++k) face.uvs[k] = uv[k];
    }
    return faces;
}

std::string objText(const std::vector<Face>& faces) {
    std::ostringstream out;
    for (const Face& face : faces) {
        for (const glm::vec3& p : face.corners) out << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
        for (const glm::vec2& t : face.uvs) out << "vt " << t.x << ' ' << t.y << '\n';
        out << "vn " << face.normal.x << ' ' << face.normal.y << ' ' << face.normal.z << '\n';
    }
    for (size_t f = 0; f < faces.size(); ++f) {
        out << 'f';
        for (size_t k = 1; k <= 4; ++k) out << ' ' << f * 4 + k << '/' << f * 4 + k << '/' << f + 1;
        out << '\n';
    }
    return out.str();
}

std::string fbxText(const std::vector<Face>& faces) {
    // Z up, -Y toward the viewer, centimeters: a meter-and-Y-up (x, y, z) is (x, -z, y) * 100 here.
    const auto file = [](const glm::vec3& v, float scale) { return glm::vec3(v.x, -v.z, v.y) * scale; };
    std::vector<glm::vec3> corners;
    std::vector<int>       polygon;
    std::map<std::tuple<float, float, float>, int> known;
    for (const Face& face : faces) {
        for (int k = 0; k < 4; ++k) {
            const glm::vec3 p = file(face.corners[k], 100.0f);
            const auto key    = std::make_tuple(p.x, p.y, p.z);
            if (!known.count(key)) {
                known[key] = static_cast<int>(corners.size());
                corners.push_back(p);
            }
            // The last corner of a polygon is written as -(index + 1).
            polygon.push_back(k == 3 ? -known[key] - 1 : known[key]);
        }
    }
    std::ostringstream out;
    out << "; FBX 7.4.0 project file\n"
           "FBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n}\n"
           "GlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {\n"
           "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",2\n"
           "\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
           "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",1\n"
           "\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",-1\n"
           "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n"
           "\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n"
           "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",1\n"
           "\t}\n}\n"
           "Objects:  {\n"
           "\tGeometry: 100, \"Geometry::Box\", \"Mesh\" {\n";
    out << fbxArray("Vertices", corners.size() * 3, [&](size_t i) {
        return corners[i / 3][static_cast<int>(i % 3)];
    });
    out << fbxArray("PolygonVertexIndex", polygon.size(), [&](size_t i) { return polygon[i]; });
    out << "\t\tGeometryVersion: 124\n"
           "\t\tLayerElementNormal: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"\"\n"
           "\t\t\tMappingInformationType: \"ByPolygonVertex\"\n\t\t\tReferenceInformationType: \"Direct\"\n";
    out << fbxArray("Normals", faces.size() * 12, [&](size_t i) {
        return file(faces[i / 12].normal, 1.0f)[static_cast<int>(i % 3)];
    });
    out << "\t\t}\n"
           "\t\tLayerElementUV: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"map\"\n"
           "\t\t\tMappingInformationType: \"ByPolygonVertex\"\n\t\t\tReferenceInformationType: \"Direct\"\n";
    out << fbxArray("UV", faces.size() * 8, [&](size_t i) {
        return faces[i / 8].uvs[(i % 8) / 2][static_cast<int>(i % 2)];
    });
    out << "\t\t}\n"
           "\t\tLayer: 0 {\n\t\t\tVersion: 100\n"
           "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementNormal\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n"
           "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementUV\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n"
           "\t\t}\n"
           "\t}\n"
           "\tModel: 200, \"Model::Box\", \"Mesh\" {\n\t\tVersion: 232\n\t\tProperties70:  {\n\t\t}\n\t}\n"
           "}\n"
           "Connections:  {\n\tC: \"OO\",100,200\n\tC: \"OO\",200,0\n}\n";
    return out.str();
}

Gltf::Gltf() {
    doc = {{"asset", {{"version", "2.0"}}}, {"scene", 0}};
}

int Gltf::view(const void* bytes, size_t size) {
    while (bin.size() % 4) bin += '\0';
    const size_t offset = bin.size();
    bin.append(static_cast<const char*>(bytes), size);
    return add("bufferViews", {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", size}});
}

int Gltf::accessor(
    const void* bytes,
    size_t count,
    int componentType,
    const char* type,
    size_t elementBytes
) {
    const int at = view(bytes, count * elementBytes);
    return add(
        "accessors",
        {{"bufferView", at}, {"componentType", componentType}, {"count", count}, {"type", type}}
    );
}

int Gltf::floats(const std::vector<float>& values) {
    return accessor(values.data(), values.size(), 5126, "SCALAR", sizeof(float));
}
int Gltf::vec2s(const std::vector<glm::vec2>& values) {
    return accessor(values.data(), values.size(), 5126, "VEC2", sizeof(glm::vec2));
}
int Gltf::vec3s(const std::vector<glm::vec3>& values) {
    return accessor(values.data(), values.size(), 5126, "VEC3", sizeof(glm::vec3));
}
int Gltf::vec4s(const std::vector<glm::vec4>& values) {
    return accessor(values.data(), values.size(), 5126, "VEC4", sizeof(glm::vec4));
}
int Gltf::mat4s(const std::vector<glm::mat4>& values) {
    return accessor(values.data(), values.size(), 5126, "MAT4", sizeof(glm::mat4));
}
int Gltf::indices(const std::vector<uint32_t>& values) {
    return accessor(values.data(), values.size(), 5125, "SCALAR", sizeof(uint32_t));
}
int Gltf::joints(const std::vector<glm::u8vec4>& values) {
    return accessor(values.data(), values.size(), 5121, "VEC4", sizeof(glm::u8vec4));
}

int Gltf::add(const char* key, nlohmann::json value) {
    nlohmann::json& list = doc[key];
    if (list.is_null()) list = nlohmann::json::array();
    list.push_back(std::move(value));
    return static_cast<int>(list.size()) - 1;
}

std::string Gltf::text() const {
    nlohmann::json out    = doc;
    const std::string uri = "data:application/octet-stream;base64," + base64(bin);
    out["buffers"] = {{{"byteLength", bin.size()}, {"uri", uri}}};
    return out.dump(1);
}

std::string Gltf::binary() const {
    nlohmann::json head = doc;
    head["buffers"] = {{{"byteLength", bin.size()}}};
    std::string json = head.dump();
    while (json.size() % 4) json += ' ';
    std::string chunk = bin;
    while (chunk.size() % 4) chunk += '\0';

    std::string out;
    put<uint32_t>(out, 0x46546C67);  // "glTF"
    put<uint32_t>(out, 2);
    put<uint32_t>(out, static_cast<uint32_t>(12 + 8 + json.size() + 8 + chunk.size()));
    put<uint32_t>(out, static_cast<uint32_t>(json.size()));
    put<uint32_t>(out, 0x4E4F534A);  // "JSON"
    out += json;
    put<uint32_t>(out, static_cast<uint32_t>(chunk.size()));
    put<uint32_t>(out, 0x004E4942);  // "BIN"
    out += chunk;
    return out;
}

int addMesh(Gltf& gltf, const std::vector<Face>& faces, int material) {
    std::vector<glm::vec3> positions, normals;
    std::vector<glm::vec2> uvs;
    std::vector<uint32_t>  indices;
    for (const Face& face : faces) {
        const auto first = static_cast<uint32_t>(positions.size());
        for (int k = 0; k < 4; ++k) {
            positions.push_back(face.corners[k]);
            normals.push_back(face.normal);
            uvs.push_back({face.uvs[k].x, 1.0f - face.uvs[k].y});
        }
        for (const uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) indices.push_back(first + k);
    }
    nlohmann::json primitive = {
        {"attributes", {{"POSITION", gltf.vec3s(positions)}, {"NORMAL", gltf.vec3s(normals)},
                        {"TEXCOORD_0", gltf.vec2s(uvs)}}},
        {"indices", gltf.indices(indices)},
    };
    if (material >= 0) primitive["material"] = material;
    return gltf.add("meshes", {{"primitives", {primitive}}});
}

std::string riggedGltf() {
    Gltf gltf;
    const std::vector<Face> faces = boxFaces({-0.25f, 0.0f, -0.25f}, {0.25f, 2.0f, 0.25f});
    std::vector<glm::u8vec4> joints;
    std::vector<glm::vec4>   weights;
    for (const Face& face : faces) {
        for (const glm::vec3& p : face.corners) {
            const bool top = p.y > 1.0f;
            joints.push_back(top ? glm::u8vec4(1, 0, 0, 0) : glm::u8vec4(0, 0, 0, 0));
            weights.push_back(top ? glm::vec4(0.75f, 0.25f, 0.0f, 0.0f) : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        }
    }
    const int mesh = addMesh(gltf, faces);
    nlohmann::json& attributes = gltf.doc["meshes"][mesh]["primitives"][0]["attributes"];
    attributes["JOINTS_0"]     = gltf.joints(joints);
    attributes["WEIGHTS_0"]    = gltf.vec4s(weights);

    glm::mat4 spineBind(1.0f);
    spineBind[3] = glm::vec4(0.0f, -1.0f, 0.0f, 1.0f);
    gltf.add("nodes", {{"name", "root"}, {"children", {1, 3}}});
    gltf.add("nodes", {{"name", "hip"}, {"children", {2}}});
    gltf.add("nodes", {{"name", "spine"}, {"translation", {0.0, 1.0, 0.0}}});
    gltf.add("nodes", {{"name", "body"}, {"mesh", mesh}, {"skin", 0}});
    const int binds = gltf.mat4s({glm::mat4(1.0f), spineBind});
    gltf.add("skins", {{"joints", {1, 2}}, {"inverseBindMatrices", binds}});
    gltf.add("scenes", {{"nodes", {0}}});

    const float half = std::sqrt(0.5f);
    nlohmann::json samplers = nlohmann::json::array();
    samplers.push_back({{"input", gltf.floats({0.0f, 1.0f})},
                        {"output", gltf.vec4s({{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, half, half}})}});
    samplers.push_back({{"input", gltf.floats({0.0f, 0.5f, 1.0f})},
                        {"output", gltf.vec3s({glm::vec3(0.0f), {0.0f, 0.5f, 0.0f}, glm::vec3(0.0f)})}});
    gltf.add("animations", {
        {"name", "sway"},
        {"samplers", samplers},
        {"channels", {{{"sampler", 0}, {"target", {{"node", 2}, {"path", "rotation"}}}},
                      {{"sampler", 1}, {"target", {{"node", 1}, {"path", "translation"}}}}}},
    });
    return gltf.text();
}

} // namespace Vkm::Test
