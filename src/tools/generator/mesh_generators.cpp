#include "mesh_generators.h"

#include <cmath>
#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <nlohmann/json.hpp>

#include "resource/asset_database.h"

namespace Engine {

namespace {
    /// Build a JSON source descriptor for a procedural mesh. Stored on the
    /// MeshAsset so SceneSerializer can recreate it on cold-start load.
    nlohmann::json meshGeneratorSource(const char* type, nlohmann::json params = nlohmann::json::object()) {
        nlohmann::json j;
        j["kind"] = "generator";
        j["type"] = type;
        if (!params.empty()) j["params"] = std::move(params);
        return j;
    }

    /**
     * @brief Stamp source + AssetId on a freshly-generated mesh in one call.
     *
     * The AssetDatabase key is "mesh:generator:<type>:<param>:<param>..."
     * derived deterministically from the same params so identical generator
     * calls map to the same GUID across runs.
     */
    void stampGenerated(MeshAsset& mesh, const char* type, const nlohmann::json& params = {}) {
        mesh.sourceJson() = meshGeneratorSource(type, params);
        std::string key = std::string("mesh:generator:") + type;
        if (params.is_object()) {
            for (auto it = params.begin(); it != params.end(); ++it) {
                key += ':';
                key += it.value().dump();
            }
        }
        mesh.assetId = AssetDatabase::get().registerOrGet(key, AssetKind::Mesh);
    }
}

MeshAsset generateTriangle(float size) {
    MeshAsset mesh;

    const glm::vec3 normal(0.0f, 1.0f, 0.0f);
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);

    mesh.vertices = {
        Vertex{ glm::vec3( 0.0f, 0.0f,  0.433f), normal, glm::vec2(0.5f, 1.0f), tangent },  // Top
        Vertex{ glm::vec3(-0.5f, 0.0f, -0.25f), normal, glm::vec2(0.0f, 0.0f), tangent },  // Bottom-left
        Vertex{ glm::vec3( 0.5f, 0.0f, -0.25f), normal, glm::vec2(1.0f, 0.0f), tangent }   // Bottom-right
    };

    mesh.indices = { 0, 1, 2 };
    mesh.computeAndSetBounds();
    stampGenerated(mesh, "triangle", {{"size", size}});
    return mesh;
}

MeshAsset generatePlane(float width, float height, uint32_t widthSegments, uint32_t heightSegments) {
    MeshAsset mesh;

    const glm::vec3 normal(0.0f, 1.0f, 0.0f);
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);

    // Simple quad plane (unit size: -0.5 to 0.5)
    mesh.vertices = {
        Vertex{ glm::vec3(-0.5f, 0.0f, -0.5f), normal, glm::vec2(0.0f, 0.0f), tangent },
        Vertex{ glm::vec3( 0.5f, 0.0f, -0.5f), normal, glm::vec2(1.0f, 0.0f), tangent },
        Vertex{ glm::vec3( 0.5f, 0.0f,  0.5f), normal, glm::vec2(1.0f, 1.0f), tangent },
        Vertex{ glm::vec3(-0.5f, 0.0f,  0.5f), normal, glm::vec2(0.0f, 1.0f), tangent }
    };

    mesh.indices = { 0, 1, 2,  2, 3, 0 };
    mesh.computeAndSetBounds();
    stampGenerated(mesh, "plane", {
        {"width", width}, {"height", height},
        {"widthSegments", widthSegments}, {"heightSegments", heightSegments}
    });
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

    const glm::vec4 tRight   ( 1.0f,  0.0f,  0.0f, 1.0f);
    const glm::vec4 tLeft    (-1.0f,  0.0f,  0.0f, 1.0f);
    const glm::vec4 tForward ( 0.0f,  0.0f,  1.0f, 1.0f);
    const glm::vec4 tBack    ( 0.0f,  0.0f, -1.0f, 1.0f);

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
    const float PI = glm::pi<float>();
    const float radius = 0.5f;  // Unit sphere (-0.5 to 0.5)

    for (uint32_t y = 0; y <= ySegments; ++y) {
        for (uint32_t x = 0; x <= xSegments; ++x) {
            float u = static_cast<float>(x) / static_cast<float>(xSegments);
            float v = static_cast<float>(y) / static_cast<float>(ySegments);
            
            float theta = u * 2.0f * PI;
            float phi = v * PI;
            
            float xPos = std::cos(theta) * std::sin(phi) * radius;
            float yPos = std::cos(phi) * radius;
            float zPos = std::sin(theta) * std::sin(phi) * radius;

            glm::vec3 position(xPos, yPos, zPos);
            glm::vec3 normal = glm::normalize(position);
            glm::vec2 uv(u, v);

            glm::vec4 tangent;
            tangent.x = -std::sin(theta) * std::sin(phi);
            tangent.y = 0.0f;
            tangent.z = std::cos(theta) * std::sin(phi);
            tangent.w = 1.0f;
            tangent = glm::normalize(tangent);

            mesh.vertices.push_back(Vertex{ position, normal, uv, tangent });
        }
    }

    for (uint32_t y = 0; y < ySegments; ++y) {
        for (uint32_t x = 0; x < xSegments; ++x) {
            uint32_t i0 = y * (xSegments + 1) + x;
            uint32_t i1 = (y + 1) * (xSegments + 1) + x;
            uint32_t i2 = (y + 1) * (xSegments + 1) + (x + 1);
            uint32_t i3 = y * (xSegments + 1) + (x + 1);

            mesh.indices.push_back(i0);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i3);
            mesh.indices.push_back(i0);
        }
    }

    mesh.computeAndSetBounds();
    stampGenerated(mesh, "sphere", {{"xSegments", xSegments}, {"ySegments", ySegments}});
    return mesh;
}

MeshAsset generatePyramid(float baseSize, float height) {
    MeshAsset mesh;

    const glm::vec3 nDown(0.0f, -1.0f, 0.0f);
    const glm::vec3 nBack  = glm::normalize(glm::vec3(0.0f, 0.5f, -0.5f));
    const glm::vec3 nRight = glm::normalize(glm::vec3(0.5f, 0.5f, 0.0f));
    const glm::vec3 nFront = glm::normalize(glm::vec3(0.0f, 0.5f, 0.5f));
    const glm::vec3 nLeft  = glm::normalize(glm::vec3(-0.5f, 0.5f, 0.0f));
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);

    // Unit pyramid (base: -0.5 to 0.5, height: 0.5)
    mesh.vertices = {
        // Base face
        Vertex{ glm::vec3(-0.5f, 0.0f, -0.5f), nDown, glm::vec2(0.0f, 0.0f), tangent },
        Vertex{ glm::vec3( 0.5f, 0.0f, -0.5f), nDown, glm::vec2(1.0f, 0.0f), tangent },
        Vertex{ glm::vec3( 0.5f, 0.0f,  0.5f), nDown, glm::vec2(1.0f, 1.0f), tangent },
        Vertex{ glm::vec3(-0.5f, 0.0f,  0.5f), nDown, glm::vec2(0.0f, 1.0f), tangent },

        // Back face
        Vertex{ glm::vec3(-0.5f, 0.0f, -0.5f), nBack, glm::vec2(0.0f, 0.0f), tangent },
        Vertex{ glm::vec3( 0.5f, 0.0f, -0.5f), nBack, glm::vec2(1.0f, 0.0f), tangent },
        Vertex{ glm::vec3( 0.0f, 0.5f,  0.0f), nBack, glm::vec2(0.5f, 1.0f), tangent },

        // Right face
        Vertex{ glm::vec3( 0.5f, 0.0f, -0.5f), nRight, glm::vec2(0.0f, 0.0f), tangent },
        Vertex{ glm::vec3( 0.5f, 0.0f,  0.5f), nRight, glm::vec2(1.0f, 0.0f), tangent },
        Vertex{ glm::vec3( 0.0f, 0.5f,  0.0f), nRight, glm::vec2(0.5f, 1.0f), tangent },

        // Front face
        Vertex{ glm::vec3( 0.5f, 0.0f,  0.5f), nFront, glm::vec2(0.0f, 0.0f), tangent },
        Vertex{ glm::vec3(-0.5f, 0.0f,  0.5f), nFront, glm::vec2(1.0f, 0.0f), tangent },
        Vertex{ glm::vec3( 0.0f, 0.5f,  0.0f), nFront, glm::vec2(0.5f, 1.0f), tangent },

        // Left face
        Vertex{ glm::vec3(-0.5f, 0.0f,  0.5f), nLeft, glm::vec2(0.0f, 0.0f), tangent },
        Vertex{ glm::vec3(-0.5f, 0.0f, -0.5f), nLeft, glm::vec2(1.0f, 0.0f), tangent },
        Vertex{ glm::vec3( 0.0f, 0.5f,  0.0f), nLeft, glm::vec2(0.5f, 1.0f), tangent }
    };

    mesh.indices = {
        0, 1, 2,  2, 3, 0,      // Base
        4, 5, 6,                // Back
        7, 8, 9,                // Right
        10, 11, 12,             // Front
        13, 14, 15              // Left
    };

    mesh.computeAndSetBounds();
    stampGenerated(mesh, "pyramid", {{"baseSize", baseSize}, {"height", height}});
    return mesh;
}

MeshAsset generateCone(
    float radius, float height, uint32_t segments
) {
    MeshAsset mesh;

    const float halfHeight = height * 0.5f;
    const glm::vec3 tip(0.0f, halfHeight, 0.0f);
    const glm::vec3 baseCenter(0.0f, -halfHeight, 0.0f);
    const glm::vec3 nDown(0.0f, -1.0f, 0.0f);
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);

    // Add tip vertex
    glm::vec3 tipNormal = glm::normalize(glm::vec3(0.0f, radius, height));
    mesh.vertices.push_back(Vertex{ tip, tipNormal, glm::vec2(0.5f, 1.0f), tangent });
    uint32_t tipIndex = 0;

    // Add base circle vertices
    uint32_t baseStartIndex = static_cast<uint32_t>(mesh.vertices.size());
    for (uint32_t i = 0; i < segments; ++i) {
        float angle = static_cast<float>(i) / static_cast<float>(segments) * glm::two_pi<float>();
        float x = std::cos(angle) * radius;
        float z = std::sin(angle) * radius;
        glm::vec3 position(x, -halfHeight, z);
        
        // Normal for cone side (pointing outward from center)
        glm::vec3 sideNormal = glm::normalize(glm::vec3(x, 0.0f, z));
        glm::vec3 coneNormal = glm::normalize(glm::vec3(sideNormal.x, radius / height, sideNormal.z));
        
        float u = static_cast<float>(i) / static_cast<float>(segments);
        mesh.vertices.push_back(Vertex{ position, coneNormal, glm::vec2(u, 0.0f), tangent });
    }

    // Add base center vertex
    mesh.vertices.push_back(Vertex{ baseCenter, nDown, glm::vec2(0.5f, 0.5f), tangent });
    uint32_t baseCenterIndex = static_cast<uint32_t>(mesh.vertices.size()) - 1;

    // Create cone side triangles (tip to base circle)
    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t nextI = (i + 1) % segments;
        mesh.indices.push_back(tipIndex);
        mesh.indices.push_back(baseStartIndex + i);
        mesh.indices.push_back(baseStartIndex + nextI);
    }

    // Create base cap triangles
    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t nextI = (i + 1) % segments;
        mesh.indices.push_back(baseCenterIndex);
        mesh.indices.push_back(baseStartIndex + nextI);
        mesh.indices.push_back(baseStartIndex + i);
    }

    mesh.computeAndSetBounds();
    stampGenerated(mesh, "cone", {
        {"radius", radius}, {"height", height}, {"segments", segments}
    });
    return mesh;
}

} // namespace Engine

