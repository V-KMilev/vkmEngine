#include "proc_mesh.h"

#include <cmath>

#include <glm/gtc/constants.hpp>

namespace Vkm::Engine {

MeshAsset makeCubeMesh() {
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
        { {-0.5f, -0.5f, -0.5f}, nFront,  {0, 0}, tRight },
        { { 0.5f, -0.5f, -0.5f}, nFront,  {1, 0}, tRight },
        { { 0.5f,  0.5f, -0.5f}, nFront,  {1, 1}, tRight },
        { {-0.5f,  0.5f, -0.5f}, nFront,  {0, 1}, tRight },

        { { 0.5f, -0.5f,  0.5f}, nBack,   {0, 0}, tLeft },
        { {-0.5f, -0.5f,  0.5f}, nBack,   {1, 0}, tLeft },
        { {-0.5f,  0.5f,  0.5f}, nBack,   {1, 1}, tLeft },
        { { 0.5f,  0.5f,  0.5f}, nBack,   {0, 1}, tLeft },

        { {-0.5f, -0.5f,  0.5f}, nLeft,   {0, 0}, tBack },
        { {-0.5f, -0.5f, -0.5f}, nLeft,   {1, 0}, tBack },
        { {-0.5f,  0.5f, -0.5f}, nLeft,   {1, 1}, tBack },
        { {-0.5f,  0.5f,  0.5f}, nLeft,   {0, 1}, tBack },

        { { 0.5f, -0.5f, -0.5f}, nRight,  {0, 0}, tForward },
        { { 0.5f, -0.5f,  0.5f}, nRight,  {1, 0}, tForward },
        { { 0.5f,  0.5f,  0.5f}, nRight,  {1, 1}, tForward },
        { { 0.5f,  0.5f, -0.5f}, nRight,  {0, 1}, tForward },

        { {-0.5f,  0.5f, -0.5f}, nTop,    {0, 0}, tRight },
        { { 0.5f,  0.5f, -0.5f}, nTop,    {1, 0}, tRight },
        { { 0.5f,  0.5f,  0.5f}, nTop,    {1, 1}, tRight },
        { {-0.5f,  0.5f,  0.5f}, nTop,    {0, 1}, tRight },

        { {-0.5f, -0.5f,  0.5f}, nBottom, {0, 0}, tRight },
        { { 0.5f, -0.5f,  0.5f}, nBottom, {1, 0}, tRight },
        { { 0.5f, -0.5f, -0.5f}, nBottom, {1, 1}, tRight },
        { {-0.5f, -0.5f, -0.5f}, nBottom, {0, 1}, tRight }
    };

    mesh.indices = {
        0, 2, 1,  0, 3, 2,
        4, 6, 5,  4, 7, 6,
        8,10, 9,  8,11,10,
        12,14,13, 12,15,14,
        16,18,17, 16,19,18,
        20,22,21, 20,23,22
    };

    mesh.boundsMin = glm::vec3(-0.5f);
    mesh.boundsMax = glm::vec3( 0.5f);
    return mesh;
}

MeshAsset makeSphereMesh(uint32_t xSegments, uint32_t ySegments) {
    MeshAsset mesh;

    if (xSegments < 3) xSegments = 3;
    if (ySegments < 2) ySegments = 2;

    constexpr float RADIUS = 0.5f;
    const float     TWO_PI = glm::two_pi<float>();
    const float     PI     = glm::pi<float>();

    // One extra ring and column so the UV seam and the poles get their own
    // vertices rather than wrapping a shared one back to u = 0.
    for (uint32_t y = 0; y <= ySegments; ++y) {
        const float v     = static_cast<float>(y) / static_cast<float>(ySegments);
        const float phi   = v * PI;
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);

        for (uint32_t x = 0; x <= xSegments; ++x) {
            const float u      = static_cast<float>(x) / static_cast<float>(xSegments);
            const float theta  = u * TWO_PI;
            const float sinTheta = std::sin(theta);
            const float cosTheta = std::cos(theta);

            const glm::vec3 normal(sinPhi * cosTheta, cosPhi, sinPhi * sinTheta);
            // d(position)/d(theta), normalised: the tangent runs along the
            // ring, which is what a normal map authored in UV space expects.
            const glm::vec3 tangent(-sinTheta, 0.0f, cosTheta);

            mesh.vertices.push_back({
                normal * RADIUS,
                normal,
                {u, 1.0f - v},
                glm::vec4(tangent, 1.0f)
            });
        }
    }

    const uint32_t stride = xSegments + 1;
    for (uint32_t y = 0; y < ySegments; ++y) {
        for (uint32_t x = 0; x < xSegments; ++x) {
            const uint32_t a = y * stride + x;
            const uint32_t b = a + stride;

            mesh.indices.insert(mesh.indices.end(), {a, a + 1, b, b, a + 1, b + 1});
        }
    }

    mesh.boundsMin = glm::vec3(-RADIUS);
    mesh.boundsMax = glm::vec3( RADIUS);
    return mesh;
}

MeshAsset makeCylinderMesh(uint32_t segments) {
    MeshAsset mesh;

    if (segments < 3) segments = 3;

    constexpr float RADIUS = 0.5f;
    constexpr float HALF_H = 0.5f;
    const float     TWO_PI = glm::two_pi<float>();

    // Side wall: one quad per segment, seam duplicated at u = 1.
    for (uint32_t i = 0; i <= segments; ++i) {
        const float u     = static_cast<float>(i) / static_cast<float>(segments);
        const float theta = u * TWO_PI;
        const float c     = std::cos(theta);
        const float s     = std::sin(theta);

        const glm::vec3 normal(c, 0.0f, s);
        const glm::vec4 tangent(-s, 0.0f, c, 1.0f);

        mesh.vertices.push_back({{c * RADIUS, -HALF_H, s * RADIUS}, normal, {u, 0.0f}, tangent});
        mesh.vertices.push_back({{c * RADIUS,  HALF_H, s * RADIUS}, normal, {u, 1.0f}, tangent});
    }

    for (uint32_t i = 0; i < segments; ++i) {
        const uint32_t a = i * 2;

        mesh.indices.insert(mesh.indices.end(), {a, a + 1, a + 2, a + 2, a + 1, a + 3});
    }

    // Caps get their own rings so the rim normal stays hard. Each is a fan
    // around a centre vertex; the bottom winds the other way to face -Y.
    const glm::vec4 capTangent(1.0f, 0.0f, 0.0f, 1.0f);
    for (int cap = 0; cap < 2; ++cap) {
        const bool      top    = (cap == 0);
        const float     y      = top ? HALF_H : -HALF_H;
        const glm::vec3 normal = top ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, -1.0f, 0.0f);

        const uint32_t centre = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({{0.0f, y, 0.0f}, normal, {0.5f, 0.5f}, capTangent});

        for (uint32_t i = 0; i < segments; ++i) {
            const float theta = static_cast<float>(i) / static_cast<float>(segments) * TWO_PI;
            const float c     = std::cos(theta);
            const float s     = std::sin(theta);

            mesh.vertices.push_back({
                {c * RADIUS, y, s * RADIUS},
                normal,
                {c * 0.5f + 0.5f, s * 0.5f + 0.5f},
                capTangent
            });
        }

        for (uint32_t i = 0; i < segments; ++i) {
            const uint32_t a = centre + 1 + i;
            const uint32_t b = centre + 1 + (i + 1) % segments;

            if (top) mesh.indices.insert(mesh.indices.end(), {centre, a, b});
            else     mesh.indices.insert(mesh.indices.end(), {centre, b, a});
        }
    }

    mesh.boundsMin = glm::vec3(-RADIUS, -HALF_H, -RADIUS);
    mesh.boundsMax = glm::vec3( RADIUS,  HALF_H,  RADIUS);
    return mesh;
}

} // namespace Vkm::Engine
