#include "basic_meshes.h"

#include <cmath>
#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace Engine {

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

    const glm::vec4 tRight ( 1.0f,  0.0f,  0.0f, 1.0f);
    const glm::vec4 tLeft  (-1.0f,  0.0f,  0.0f, 1.0f);
    const glm::vec4 tForward(0.0f,  0.0f,  1.0f, 1.0f);
    const glm::vec4 tBack   (0.0f,  0.0f, -1.0f, 1.0f);

    // Unit cube (-0.5 to 0.5)
    mesh.vertices = {
        // Front face (-Z)
        Vertex{ glm::vec3(-0.5f, -0.5f, -0.5f), nFront, glm::vec2(0.0f, 0.0f), tRight },
        Vertex{ glm::vec3( 0.5f, -0.5f, -0.5f), nFront, glm::vec2(1.0f, 0.0f), tRight },
        Vertex{ glm::vec3( 0.5f,  0.5f, -0.5f), nFront, glm::vec2(1.0f, 1.0f), tRight },
        Vertex{ glm::vec3(-0.5f,  0.5f, -0.5f), nFront, glm::vec2(0.0f, 1.0f), tRight },

        // Back face (+Z)
        Vertex{ glm::vec3( 0.5f, -0.5f,  0.5f), nBack, glm::vec2(0.0f, 0.0f), tLeft },
        Vertex{ glm::vec3(-0.5f, -0.5f,  0.5f), nBack, glm::vec2(1.0f, 0.0f), tLeft },
        Vertex{ glm::vec3(-0.5f,  0.5f,  0.5f), nBack, glm::vec2(1.0f, 1.0f), tLeft },
        Vertex{ glm::vec3( 0.5f,  0.5f,  0.5f), nBack, glm::vec2(0.0f, 1.0f), tLeft },

        // Left face (-X)
        Vertex{ glm::vec3(-0.5f, -0.5f,  0.5f), nLeft, glm::vec2(0.0f, 0.0f), tBack },
        Vertex{ glm::vec3(-0.5f, -0.5f, -0.5f), nLeft, glm::vec2(1.0f, 0.0f), tBack },
        Vertex{ glm::vec3(-0.5f,  0.5f, -0.5f), nLeft, glm::vec2(1.0f, 1.0f), tBack },
        Vertex{ glm::vec3(-0.5f,  0.5f,  0.5f), nLeft, glm::vec2(0.0f, 1.0f), tBack },

        // Right face (+X)
        Vertex{ glm::vec3( 0.5f, -0.5f, -0.5f), nRight, glm::vec2(0.0f, 0.0f), tForward },
        Vertex{ glm::vec3( 0.5f, -0.5f,  0.5f), nRight, glm::vec2(1.0f, 0.0f), tForward },
        Vertex{ glm::vec3( 0.5f,  0.5f,  0.5f), nRight, glm::vec2(1.0f, 1.0f), tForward },
        Vertex{ glm::vec3( 0.5f,  0.5f, -0.5f), nRight, glm::vec2(0.0f, 1.0f), tForward },

        // Top face (+Y)
        Vertex{ glm::vec3(-0.5f,  0.5f, -0.5f), nTop, glm::vec2(0.0f, 0.0f), tRight },
        Vertex{ glm::vec3( 0.5f,  0.5f, -0.5f), nTop, glm::vec2(1.0f, 0.0f), tRight },
        Vertex{ glm::vec3( 0.5f,  0.5f,  0.5f), nTop, glm::vec2(1.0f, 1.0f), tRight },
        Vertex{ glm::vec3(-0.5f,  0.5f,  0.5f), nTop, glm::vec2(0.0f, 1.0f), tRight },

        // Bottom face (-Y)
        Vertex{ glm::vec3(-0.5f, -0.5f,  0.5f), nBottom, glm::vec2(0.0f, 0.0f), tRight },
        Vertex{ glm::vec3( 0.5f, -0.5f,  0.5f), nBottom, glm::vec2(1.0f, 0.0f), tRight },
        Vertex{ glm::vec3( 0.5f, -0.5f, -0.5f), nBottom, glm::vec2(1.0f, 1.0f), tRight },
        Vertex{ glm::vec3(-0.5f, -0.5f, -0.5f), nBottom, glm::vec2(0.0f, 1.0f), tRight }
    };

    mesh.indices = {
        0, 1, 2,  2, 3, 0,      // Front
        4, 5, 6,  6, 7, 4,      // Back
        8, 9, 10, 10, 11, 8,    // Left
        12, 13, 14, 14, 15, 12, // Right
        16, 17, 18, 18, 19, 16, // Top
        20, 21, 22, 22, 23, 20  // Bottom
    };

    mesh.computeAndSetBounds();
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
    return mesh;
}

} // namespace Engine

