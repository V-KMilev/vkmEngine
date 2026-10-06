#include "resource/generate/mesh_generators.h"

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <nlohmann/json.hpp>

#include "l_assert.h"

#include "resource/asset_source_kind.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

namespace {
/**
 * @brief Build a JSON source descriptor for a procedural mesh.
 *
 * The MeshAsset's recipe, which createGeneratedMesh rebuilds it from.
 *
 * @param type Generator type tag (e.g. "cube", "sphere").
 * @param params Optional generator parameters to embed under "params".
 * @return The "generator" source descriptor JSON.
 */
nlohmann::json meshGeneratorSource(const char* type, nlohmann::json params = nlohmann::json::object()) {
    nlohmann::json j;
    j["kind"] = AssetSourceKind::GENERATOR;
    j["type"] = type;
    if (!params.empty()) j["params"] = std::move(params);
    return j;
}

/**
 * @brief Spell a generator descriptor as the name the mesh is registered under.
 *
 * "mesh:generator:<type>:<param>:<param>...", parameters in nlohmann's sorted
 * key order, so the name is a function of the recipe alone.
 *
 * @param source A "generator" descriptor as meshGeneratorSource builds it.
 * @return The name the mesh belongs under.
 */
std::string generatorName(const nlohmann::json& source) {
    std::string key = "mesh:generator:" + source.value("type", std::string{});
    const auto params = source.find("params");
    if (params != source.end() && params->is_object()) {
        for (auto it = params->begin(); it != params->end(); ++it) {
            key += ':';
            key += it.value().dump();
        }
    }
    return key;
}

/**
 * @brief Stamp the generator descriptor on a freshly-generated mesh.
 *
 * @param mesh Freshly generated mesh to stamp (source set in place).
 * @param type Generator type tag (e.g. "cube", "sphere").
 * @param params Generator parameters folded into the descriptor.
 */
void stampGenerated(MeshAsset& mesh, const char* type, const nlohmann::json& params = {}) {
    mesh.sourceJson() = meshGeneratorSource(type, params);
}

} // namespace

MeshHandle addGeneratedMesh(ResourceManager& resources, MeshAsset mesh) {
    VKM_ASSERT(mesh.hasSource(), "addGeneratedMesh: the mesh carries no generator descriptor");
    const std::string name = generatorName(mesh.sourceJson());
    if (auto existing = resources.findByName<MeshAsset>(name)) return existing;
    return resources.add(std::move(mesh), name);
}

MeshHandle createGeneratedMesh(const nlohmann::json& source, ResourceManager& resources) {
    const std::string type = source.value("type", std::string{});
    const auto& p = source.contains("params") ? source["params"] : nlohmann::json::object();

    MeshAsset mesh;
    if (type == "cube") {
        mesh = generateCube();
    } else if (type == "sphere") {
        mesh = generateSphere(p.value("xSegments", 32u), p.value("ySegments", 16u));
    } else if (type == "cone") {
        mesh = generateCone(p.value("radius", 0.5f), p.value("height", 1.0f), p.value("segments", 16u));
    } else if (type == "cylinder") {
        mesh = generateCylinder(p.value("radius", 0.5f), p.value("height", 1.0f), p.value("segments", 20u));
    } else if (type == "pyramid") {
        mesh = generatePyramid(p.value("baseSize", 1.0f), p.value("height", 1.0f));
    } else if (type == "plane") {
        mesh = generatePlane(
            p.value("width", 1.0f),
            p.value("height", 1.0f),
            p.value("widthSegments", 1u),
            p.value("heightSegments", 1u)
        );
    } else if (type == "triangle") {
        mesh = generateTriangle(p.value("size", 1.0f));
    } else {
        return {};
    }

    if (mesh.vertices.empty()) return {};
    return resources.add(std::move(mesh));
}

MeshAsset generateTriangle(float size) {
    MeshAsset mesh;

    const glm::vec3 normal(0.0f, 1.0f, 0.0f);
    // V grows toward +Z and cross(+Y, +X) is -Z, so w is -1.
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, -1.0f);

    mesh.vertices = {
        // Top
        Vertex{ glm::vec3( 0.0f, 0.0f,  0.433f) * size, normal, glm::vec2(0.5f, 1.0f), tangent },
        // Bottom-left
        Vertex{ glm::vec3(-0.5f, 0.0f, -0.25f) * size, normal, glm::vec2(0.0f, 0.0f), tangent },
        // Bottom-right
        Vertex{ glm::vec3( 0.5f, 0.0f, -0.25f) * size, normal, glm::vec2(1.0f, 0.0f), tangent }
    };

    // CCW seen from +Y, so cross(v1-v0, v2-v0) == +Y, the normal side.
    mesh.indices = { 0, 2, 1 };
    mesh.computeAndSetBounds();
    stampGenerated(mesh, "triangle", {{"size", size}});
    return mesh;
}

MeshAsset generatePlane(float width, float height, uint32_t widthSegments, uint32_t heightSegments) {
    MeshAsset mesh;

    const glm::vec3 normal(0.0f, 1.0f, 0.0f);
    // V grows toward +Z below, and cross(+Y, +X) is -Z, so w is -1.
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, -1.0f);

    // u along x, v along z; each cell CCW seen from +Y, the normal side.
    const uint32_t nx = widthSegments  > 0 ? widthSegments  : 1;
    const uint32_t nz = heightSegments > 0 ? heightSegments : 1;
    const float halfW = width  * 0.5f;
    const float halfH = height * 0.5f;

    mesh.vertices.reserve((nx + 1) * (nz + 1));
    for (uint32_t j = 0; j <= nz; ++j) {
        const float v = static_cast<float>(j) / static_cast<float>(nz);
        const float z = -halfH + v * height;
        for (uint32_t i = 0; i <= nx; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(nx);
            const float x = -halfW + u * width;
            mesh.vertices.push_back(Vertex{ glm::vec3(x, 0.0f, z), normal, glm::vec2(u, v), tangent });
        }
    }

    const uint32_t stride = nx + 1;
    mesh.indices.reserve(nx * nz * 6);
    for (uint32_t j = 0; j < nz; ++j) {
        for (uint32_t i = 0; i < nx; ++i) {
            const uint32_t v00 =  j      * stride + i;
            const uint32_t v10 =  j      * stride + (i + 1);
            const uint32_t v11 = (j + 1) * stride + (i + 1);
            const uint32_t v01 = (j + 1) * stride + i;
            mesh.indices.push_back(v00);
            mesh.indices.push_back(v11);
            mesh.indices.push_back(v10);
            mesh.indices.push_back(v11);
            mesh.indices.push_back(v00);
            mesh.indices.push_back(v01);
        }
    }

    mesh.computeAndSetBounds();
    const nlohmann::json params = {
        {"width", width},
        {"height", height},
        {"widthSegments", widthSegments},
        {"heightSegments", heightSegments}
    };
    stampGenerated(mesh, "plane", params);
    return mesh;
}

MeshAsset generateCube() {
    MeshAsset mesh;

    const glm::vec3 nFront ( 0.0f,  0.0f, -1.0f);
    const glm::vec3 nBack  ( 0.0f,  0.0f,  1.0f);
    const glm::vec3 nLeft  (-1.0f,  0.0f,  0.0f);
    const glm::vec3 nRight ( 1.0f,  0.0f,  0.0f);
    const glm::vec3 nTop   ( 0.0f,  1.0f,  0.0f);
    const glm::vec3 nBottom( 0.0f, -1.0f,  0.0f);

    // On every face V grows against cross(normal, tangent): on the front, V
    // climbs +Y while cross(-Z, +X) is -Y. So w is -1 on all six.
    const glm::vec4 tRight   ( 1.0f,  0.0f,  0.0f, -1.0f);
    const glm::vec4 tLeft    (-1.0f,  0.0f,  0.0f, -1.0f);
    const glm::vec4 tForward ( 0.0f,  0.0f,  1.0f, -1.0f);
    const glm::vec4 tBack    ( 0.0f,  0.0f, -1.0f, -1.0f);

    mesh.vertices = {
        // Front (-Z)
        { {-0.5f, -0.5f, -0.5f}, nFront,  {0, 0}, tRight },
        { { 0.5f, -0.5f, -0.5f}, nFront,  {1, 0}, tRight },
        { { 0.5f,  0.5f, -0.5f}, nFront,  {1, 1}, tRight },
        { {-0.5f,  0.5f, -0.5f}, nFront,  {0, 1}, tRight },

        // Back (+Z)
        { { 0.5f, -0.5f,  0.5f}, nBack,   {0, 0}, tLeft },
        { {-0.5f, -0.5f,  0.5f}, nBack,   {1, 0}, tLeft },
        { {-0.5f,  0.5f,  0.5f}, nBack,   {1, 1}, tLeft },
        { { 0.5f,  0.5f,  0.5f}, nBack,   {0, 1}, tLeft },

        // Left (-X)
        { {-0.5f, -0.5f,  0.5f}, nLeft,   {0, 0}, tBack },
        { {-0.5f, -0.5f, -0.5f}, nLeft,   {1, 0}, tBack },
        { {-0.5f,  0.5f, -0.5f}, nLeft,   {1, 1}, tBack },
        { {-0.5f,  0.5f,  0.5f}, nLeft,   {0, 1}, tBack },

        // Right (+X)
        { { 0.5f, -0.5f, -0.5f}, nRight,  {0, 0}, tForward },
        { { 0.5f, -0.5f,  0.5f}, nRight,  {1, 0}, tForward },
        { { 0.5f,  0.5f,  0.5f}, nRight,  {1, 1}, tForward },
        { { 0.5f,  0.5f, -0.5f}, nRight,  {0, 1}, tForward },

        // Top (+Y)
        { {-0.5f,  0.5f, -0.5f}, nTop,    {0, 0}, tRight },
        { { 0.5f,  0.5f, -0.5f}, nTop,    {1, 0}, tRight },
        { { 0.5f,  0.5f,  0.5f}, nTop,    {1, 1}, tRight },
        { {-0.5f,  0.5f,  0.5f}, nTop,    {0, 1}, tRight },

        // Bottom (-Y)
        { {-0.5f, -0.5f,  0.5f}, nBottom, {0, 0}, tRight },
        { { 0.5f, -0.5f,  0.5f}, nBottom, {1, 0}, tRight },
        { { 0.5f, -0.5f, -0.5f}, nBottom, {1, 1}, tRight },
        { {-0.5f, -0.5f, -0.5f}, nBottom, {0, 1}, tRight }
    };

    // CCW winding for all faces (outside view)
    mesh.indices = {
        0, 2, 1,  0, 3, 2,      // Front
        4, 6, 5,  4, 7, 6,      // Back
        8,10, 9,  8,11,10,      // Left
        12,14,13, 12,15,14,    // Right
        16,18,17, 16,19,18,    // Top
        20,22,21, 20,23,22     // Bottom
    };

    mesh.computeAndSetBounds();
    stampGenerated(mesh, "cube");
    return mesh;
}

MeshAsset generateSphere(uint32_t xSegments, uint32_t ySegments) {
    MeshAsset mesh;
    const float radius = 0.5f;

    // Each cube face maps its own [0,1] UVs.
    const uint32_t res = std::max(2u, std::max(xSegments, ySegments) / 2u);  // per-face grid

    // Flip a triangle whose geometric normal points inward, so winding holds
    // whatever the handedness of each face basis.
    auto emitTri = [&mesh](uint32_t a, uint32_t b, uint32_t c) {
        const glm::vec3& pa = mesh.vertices[a].position;
        const glm::vec3  gn = glm::cross(mesh.vertices[b].position - pa, mesh.vertices[c].position - pa);
        if (glm::dot(gn, pa) < 0.0f) std::swap(b, c);
        mesh.indices.push_back(a);
        mesh.indices.push_back(b);
        mesh.indices.push_back(c);
    };

    const glm::vec3 faceN[6] = {
        { 1, 0, 0}, {-1, 0, 0},
        { 0, 1, 0}, { 0,-1, 0},
        { 0, 0, 1}, { 0, 0,-1},
    };

    for (int f = 0; f < 6; ++f) {
        const glm::vec3 n = faceN[f];
        // Any orthonormal in-plane basis for this face.
        const glm::vec3 helper = (std::abs(n.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        const glm::vec3 uAxis = glm::normalize(glm::cross(helper, n));
        const glm::vec3 vAxis = glm::cross(n, uAxis);

        const uint32_t base   = static_cast<uint32_t>(mesh.vertices.size());
        const uint32_t stride = res + 1;

        for (uint32_t j = 0; j <= res; ++j) {
            for (uint32_t i = 0; i <= res; ++i) {
                const float s = static_cast<float>(i) / static_cast<float>(res) * 2.0f - 1.0f;
                const float t = static_cast<float>(j) / static_cast<float>(res) * 2.0f - 1.0f;

                const glm::vec3 dir      = glm::normalize(n + uAxis * s + vAxis * t);
                const glm::vec3 position = dir * radius;
                const glm::vec3 normal   = dir;
                const glm::vec2 uv(
                    static_cast<float>(i) / static_cast<float>(res),
                    static_cast<float>(j) / static_cast<float>(res)
                );

                // Face u-axis projected into the tangent plane; never zero, since
                // dir always keeps a +n component.
                const glm::vec3 tDir = glm::normalize(uAxis - dir * glm::dot(uAxis, dir));
                const glm::vec4 tangent(tDir, 1.0f);

                mesh.vertices.push_back(Vertex{ position, normal, uv, tangent });
            }
        }

        for (uint32_t j = 0; j < res; ++j) {
            for (uint32_t i = 0; i < res; ++i) {
                const uint32_t i0 = base + j * stride + i;
                const uint32_t i1 = base + j * stride + (i + 1);
                const uint32_t i2 = base + (j + 1) * stride + (i + 1);
                const uint32_t i3 = base + (j + 1) * stride + i;
                emitTri(i0, i1, i2);
                emitTri(i0, i2, i3);
            }
        }
    }

    mesh.computeAndSetBounds();
    stampGenerated(mesh, "sphere", {{"xSegments", xSegments}, {"ySegments", ySegments}});
    return mesh;
}

MeshAsset generatePyramid(float baseSize, float height) {
    MeshAsset mesh;

    const float h = baseSize * 0.5f;

    // One tangent per face, along U: +X on the base, each side's own base edge.
    // V grows along cross(-Y, +X) = +Z on the base, so w is +1 there, and
    // against the cross product on the sides, where it climbs to the apex.
    const glm::vec4 tBase ( 1.0f, 0.0f,  0.0f,  1.0f);
    const glm::vec4 tBack ( 1.0f, 0.0f,  0.0f, -1.0f);
    const glm::vec4 tRight( 0.0f, 0.0f,  1.0f, -1.0f);
    const glm::vec4 tFront(-1.0f, 0.0f,  0.0f, -1.0f);
    const glm::vec4 tLeft ( 0.0f, 0.0f, -1.0f, -1.0f);

    const glm::vec3 bl(-h, 0.0f, -h);
    const glm::vec3 br( h, 0.0f, -h);
    const glm::vec3 fr( h, 0.0f,  h);
    const glm::vec3 fl(-h, 0.0f,  h);
    const glm::vec3 apex(0.0f, height, 0.0f);
    const glm::vec3 nDown(0.0f, -1.0f, 0.0f);

    // Each side winds (a, c, b) below, so its outward normal is cross(c-a, b-a).
    auto faceNormal = [](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
        return glm::normalize(glm::cross(c - a, b - a));
    };
    const glm::vec3 nBack  = faceNormal(bl, br, apex);
    const glm::vec3 nRight = faceNormal(br, fr, apex);
    const glm::vec3 nFront = faceNormal(fr, fl, apex);
    const glm::vec3 nLeft  = faceNormal(fl, bl, apex);

    mesh.vertices = {
        // Base face
        Vertex{ bl, nDown, glm::vec2(0.0f, 0.0f), tBase },
        Vertex{ br, nDown, glm::vec2(1.0f, 0.0f), tBase },
        Vertex{ fr, nDown, glm::vec2(1.0f, 1.0f), tBase },
        Vertex{ fl, nDown, glm::vec2(0.0f, 1.0f), tBase },

        // Back face
        Vertex{ bl,   nBack, glm::vec2(0.0f, 0.0f), tBack },
        Vertex{ br,   nBack, glm::vec2(1.0f, 0.0f), tBack },
        Vertex{ apex, nBack, glm::vec2(0.5f, 1.0f), tBack },

        // Right face
        Vertex{ br,   nRight, glm::vec2(0.0f, 0.0f), tRight },
        Vertex{ fr,   nRight, glm::vec2(1.0f, 0.0f), tRight },
        Vertex{ apex, nRight, glm::vec2(0.5f, 1.0f), tRight },

        // Front face
        Vertex{ fr,   nFront, glm::vec2(0.0f, 0.0f), tFront },
        Vertex{ fl,   nFront, glm::vec2(1.0f, 0.0f), tFront },
        Vertex{ apex, nFront, glm::vec2(0.5f, 1.0f), tFront },

        // Left face
        Vertex{ fl,   nLeft, glm::vec2(0.0f, 0.0f), tLeft },
        Vertex{ bl,   nLeft, glm::vec2(1.0f, 0.0f), tLeft },
        Vertex{ apex, nLeft, glm::vec2(0.5f, 1.0f), tLeft }
    };

    // Base CCW seen from below (-Y). Sides wind (base-left, apex, base-right),
    // so cross(v1-v0, v2-v0) points outward.
    mesh.indices = {
        0, 1, 2,  2, 3, 0,      // Base
        4, 6, 5,                // Back
        7, 9, 8,                // Right
        10, 12, 11,             // Front
        13, 15, 14              // Left
    };

    mesh.computeAndSetBounds();
    stampGenerated(mesh, "pyramid", {{"baseSize", baseSize}, {"height", height}});
    return mesh;
}

MeshAsset generateCone(float radius, float height, uint32_t segments) {
    MeshAsset mesh;

    // Three is the fewest that closes a ring; a recipe can hold any number.
    if (segments < 3) segments = 3;

    const float     halfHeight = height * 0.5f;
    const float     twoPi      = glm::two_pi<float>();
    const glm::vec3 tip(0.0f, halfHeight, 0.0f);
    const glm::vec3 nDown(0.0f, -1.0f, 0.0f);

    // radial * height + up * radius points where the gradient does, and is still
    // a direction at height 0. The same all the way round.
    const float slope        = std::sqrt(height * height + radius * radius);
    const float normalRadial = slope > 0.0f ? height / slope : 0.0f;
    const float normalUp     = slope > 0.0f ? radius / slope : 1.0f;

    // The rim, with the seam column repeated at u = 1 so the texture wraps once.
    for (uint32_t i = 0; i <= segments; ++i) {
        const float u     = static_cast<float>(i) / static_cast<float>(segments);
        const float angle = u * twoPi;
        // From the angle, not the rim point: at radius 0 normalizing that is NaN.
        const glm::vec3 radial(std::cos(angle), 0.0f, std::sin(angle));
        // Along U; V climbs against cross(normal, tangent), so w is -1.
        const glm::vec4 tangent(-std::sin(angle), 0.0f, std::cos(angle), -1.0f);
        mesh.vertices.push_back(Vertex{
            radial * radius + glm::vec3(0.0f, -halfHeight, 0.0f),
            radial * normalRadial + glm::vec3(0.0f, normalUp, 0.0f),
            glm::vec2(u, 0.0f),
            tangent
        });
    }

    // One apex per side for its own tangent and UV; every copy's normal is the
    // axis, so the tip lights the same from any side.
    const glm::vec3 tipNormal(0.0f, 1.0f, 0.0f);
    for (uint32_t i = 0; i < segments; ++i) {
        const float    mid   = (static_cast<float>(i) + 0.5f) / static_cast<float>(segments);
        const float    angle = mid * twoPi;
        const uint32_t apex  = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(Vertex{
            tip,
            tipNormal,
            glm::vec2(mid, 1.0f),
            glm::vec4(-std::sin(angle), 0.0f, std::cos(angle), -1.0f)
        });
        // (apex, next, i) faces outward; (apex, i, next) would face inward.
        mesh.indices.push_back(apex);
        mesh.indices.push_back(i + 1);
        mesh.indices.push_back(i);
    }

    // Own ring for a hard rim; U along +X, V along +Z = cross(-Y, +X), so w is +1.
    const uint32_t  centre = static_cast<uint32_t>(mesh.vertices.size());
    const glm::vec4 capTangent(1.0f, 0.0f, 0.0f, 1.0f);
    mesh.vertices.push_back(Vertex{ {0.0f, -halfHeight, 0.0f}, nDown, {0.5f, 0.5f}, capTangent });
    for (uint32_t i = 0; i < segments; ++i) {
        const float angle = static_cast<float>(i) / static_cast<float>(segments) * twoPi;
        const float c     = std::cos(angle);
        const float s     = std::sin(angle);
        mesh.vertices.push_back(Vertex{
            {c * radius, -halfHeight, s * radius},
            nDown,
            {c * 0.5f + 0.5f, s * 0.5f + 0.5f},
            capTangent
        });
    }
    // The ring runs from +X toward +Z, clockwise seen from above, so the fan
    // (centre, i, next) faces -Y.
    for (uint32_t i = 0; i < segments; ++i) {
        mesh.indices.push_back(centre);
        mesh.indices.push_back(centre + 1 + i);
        mesh.indices.push_back(centre + 1 + (i + 1) % segments);
    }

    mesh.computeAndSetBounds();
    stampGenerated(mesh, "cone", {{"radius", radius}, {"height", height}, {"segments", segments}});
    return mesh;
}

MeshAsset generateCylinder(float radius, float height, uint32_t segments) {
    MeshAsset mesh;

    if (segments < 3) segments = 3;

    const float halfHeight = height * 0.5f;
    const float twoPi      = glm::two_pi<float>();

    // Side wall: one quad per segment, with the seam column duplicated at u = 1.
    for (uint32_t i = 0; i <= segments; ++i) {
        const float u     = static_cast<float>(i) / static_cast<float>(segments);
        const float theta = u * twoPi;
        const float c     = std::cos(theta);
        const float s     = std::sin(theta);

        const glm::vec3 normal(c, 0.0f, s);
        // Along the wall, which is U; V climbs against cross(normal, tangent),
        // so w is -1.
        const glm::vec4 tangent(-s, 0.0f, c, -1.0f);

        mesh.vertices.push_back({{c * radius, -halfHeight, s * radius}, normal, {u, 0.0f}, tangent});
        mesh.vertices.push_back({{c * radius,  halfHeight, s * radius}, normal, {u, 1.0f}, tangent});
    }

    for (uint32_t i = 0; i < segments; ++i) {
        const uint32_t a = i * 2;
        mesh.indices.insert(mesh.indices.end(), {a, a + 1, a + 2, a + 2, a + 1, a + 3});
    }

    // Caps get their own rings for a hard rim. U along +X, V along +Z, so w is
    // -1 on top (cross(+Y, +X) is -Z) and +1 underneath.
    for (int cap = 0; cap < 2; ++cap) {
        const bool      top    = (cap == 0);
        const float     y      = top ? halfHeight : -halfHeight;
        const glm::vec3 normal = top ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, -1.0f, 0.0f);
        const glm::vec4 capTangent(1.0f, 0.0f, 0.0f, top ? -1.0f : 1.0f);

        const uint32_t centre = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({{0.0f, y, 0.0f}, normal, {0.5f, 0.5f}, capTangent});

        for (uint32_t i = 0; i < segments; ++i) {
            const float theta = static_cast<float>(i) / static_cast<float>(segments) * twoPi;
            const float c     = std::cos(theta);
            const float s     = std::sin(theta);

            mesh.vertices.push_back({
                {c * radius, y, s * radius},
                normal,
                {c * 0.5f + 0.5f, s * 0.5f + 0.5f},
                capTangent
            });
        }

        for (uint32_t i = 0; i < segments; ++i) {
            const uint32_t a = centre + 1 + i;
            const uint32_t b = centre + 1 + (i + 1) % segments;

            // The ring runs from +X toward +Z, clockwise seen from above, so the
            // top fans (centre, next, i) to face +Y and the bottom the other way.
            if (top) mesh.indices.insert(mesh.indices.end(), {centre, b, a});
            else     mesh.indices.insert(mesh.indices.end(), {centre, a, b});
        }
    }

    // Set, not measured: the ring's cos/sin never quite reach the radius.
    mesh.boundsMin = glm::vec3(-radius, -halfHeight, -radius);
    mesh.boundsMax = glm::vec3( radius,  halfHeight,  radius);
    stampGenerated(mesh, "cylinder", {{"radius", radius}, {"height", height}, {"segments", segments}});
    return mesh;
}

} // namespace Vkm::Engine

