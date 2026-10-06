#include "system/physics/collision/mesh_bvh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include <glm/gtc/constants.hpp>

namespace Vkm::Engine {

namespace {

// At or below this many triangles a node is a leaf; splitting further costs more than the tests saved.
constexpr uint32_t LEAF_TRIANGLES = 4;

struct Bounds {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

Bounds boundsOf(const std::vector<glm::vec3>& triangles, uint32_t first, uint32_t count) {
    Bounds bounds;
    bounds.min = glm::vec3(std::numeric_limits<float>::max());
    bounds.max = glm::vec3(std::numeric_limits<float>::lowest());
    for (uint32_t t = first; t < first + count; ++t) {
        for (uint32_t c = 0; c < 3; ++c) {
            const glm::vec3& point = triangles[t * 3 + c];
            bounds.min = glm::min(bounds.min, point);
            bounds.max = glm::max(bounds.max, point);
        }
    }
    return bounds;
}

glm::vec3 centroidOf(const std::vector<glm::vec3>& triangles, uint32_t triangle) {
    return (triangles[triangle * 3 + 0] + triangles[triangle * 3 + 1] + triangles[triangle * 3 + 2]) / 3.0f;
}

void swapTriangles(std::vector<glm::vec3>& triangles, uint32_t a, uint32_t b) {
    if (a == b) return;
    for (uint32_t c = 0; c < 3; ++c) {
        std::swap(triangles[a * 3 + c], triangles[b * 3 + c]);
    }
}

uint32_t buildNode(
    std::vector<glm::vec3>& triangles,
    std::vector<MeshNode>& nodes,
    uint32_t first,
    uint32_t count
) {
    const uint32_t self = static_cast<uint32_t>(nodes.size());
    nodes.push_back({});

    const Bounds bounds = boundsOf(triangles, first, count);
    nodes[self].min = bounds.min;
    nodes[self].max = bounds.max;

    if (count <= LEAF_TRIANGLES) {
        nodes[self].firstTriangle = first;
        nodes[self].triangleCount = count;
        return self;
    }

    // Widest axis, median split.
    const glm::vec3 extent = bounds.max - bounds.min;
    int axis = 0;
    if (extent.y > extent[axis]) axis = 1;
    if (extent.z > extent[axis]) axis = 2;

    // Stable, not nth_element: the order within each half is contact order, and nth_element's differs
    // between libstdc++ and MSVC, so one world would simulate differently.
    std::vector<std::pair<float, uint32_t>> order(count);
    for (uint32_t i = 0; i < count; ++i) {
        order[i] = {centroidOf(triangles, first + i)[axis], first + i};
    }
    const auto byCentroid = [](const std::pair<float, uint32_t>& a, const std::pair<float, uint32_t>& b) {
        return a.first < b.first;
    };
    std::stable_sort(order.begin(), order.end(), byCentroid);

    // Every swap puts at least one triangle where it belongs, so this terminates.
    std::vector<uint32_t> slotOf(count);
    for (uint32_t i = 0; i < count; ++i) slotOf[order[i].second - first] = i;
    for (uint32_t i = 0; i < count; ++i) {
        while (slotOf[i] != i) {
            const uint32_t target = slotOf[i];
            swapTriangles(triangles, first + i, first + target);
            std::swap(slotOf[i], slotOf[target]);
        }
    }

    const uint32_t leftCount = count / 2;
    buildNode(triangles, nodes, first, leftCount);
    const uint32_t right = buildNode(triangles, nodes, first + leftCount, count - leftCount);
    nodes[self].rightChild = right;
    nodes[self].triangleCount = 0;
    return self;
}

// Whether the segment from origin along dir for length passes through the node
// grown by grow, by slabs. inv holds 1 / dir on every axis dir is not parallel to.
bool segmentMeetsNode(
    const MeshNode& node,
    const glm::vec3& origin,
    const glm::vec3& dir,
    const glm::vec3& inv,
    float length,
    float grow
) {
    float enter = 0.0f;
    float leave = length;
    for (int i = 0; i < 3; ++i) {
        const float lo = node.min[i] - grow;
        const float hi = node.max[i] + grow;
        if (std::fabs(dir[i]) < glm::epsilon<float>()) {
            if (origin[i] < lo || origin[i] > hi) return false;
            continue;
        }
        float t0 = (lo - origin[i]) * inv[i];
        float t1 = (hi - origin[i]) * inv[i];
        if (t0 > t1) std::swap(t0, t1);
        enter = std::max(enter, t0);
        leave = std::min(leave, t1);
        if (enter > leave) return false;
    }
    return true;
}

} // namespace

std::vector<MeshNode> buildMeshBvh(std::vector<glm::vec3>& triangles) {
    const uint32_t count = static_cast<uint32_t>(triangles.size() / 3);
    std::vector<MeshNode> nodes;
    if (count == 0) return nodes;

    nodes.reserve(count * 2);
    buildNode(triangles, nodes, 0, count);
    return nodes;
}

void queryMeshBvh(
    const std::vector<MeshNode>& nodes,
    const glm::vec3& min,
    const glm::vec3& max,
    std::vector<uint32_t>& out
) {
    if (nodes.empty()) return;

    std::array<uint32_t, 64> stack{};
    int top = 0;
    stack[top++] = 0;

    while (top > 0) {
        const MeshNode& node = nodes[stack[--top]];
        if (node.min.x > max.x || node.max.x < min.x) continue;
        if (node.min.y > max.y || node.max.y < min.y) continue;
        if (node.min.z > max.z || node.max.z < min.z) continue;

        if (node.triangleCount > 0) {
            for (uint32_t i = 0; i < node.triangleCount; ++i) {
                out.push_back(node.firstTriangle + i);
            }
            continue;
        }

        // A full stack drops a branch (losing contacts) rather than overflow; a box query has no near
        // side, so with one slot left the left child takes it.
        const uint32_t left = static_cast<uint32_t>(&node - nodes.data()) + 1;
        const int room = static_cast<int>(stack.size()) - top;
        if (room >= 2) stack[top++] = node.rightChild;
        if (room >= 1) stack[top++] = left;
    }
}

void queryMeshBvhSegment(
    const std::vector<MeshNode>& nodes,
    const glm::vec3& origin,
    const glm::vec3& dir,
    float length,
    float grow,
    std::vector<uint32_t>& out
) {
    if (nodes.empty()) return;

    glm::vec3 inv(0.0f);
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dir[i]) >= glm::epsilon<float>()) inv[i] = 1.0f / dir[i];
    }

    std::array<uint32_t, 64> stack{};
    int top = 0;
    stack[top++] = 0;

    while (top > 0) {
        const MeshNode& node = nodes[stack[--top]];
        if (!segmentMeetsNode(node, origin, dir, inv, length, grow)) continue;

        if (node.triangleCount > 0) {
            for (uint32_t i = 0; i < node.triangleCount; ++i) {
                out.push_back(node.firstTriangle + i);
            }
            continue;
        }

        // As queryMeshBvh: a full stack keeps the left child.
        const uint32_t left = static_cast<uint32_t>(&node - nodes.data()) + 1;
        const int room = static_cast<int>(stack.size()) - top;
        if (room >= 2) stack[top++] = node.rightChild;
        if (room >= 1) stack[top++] = left;
    }
}

} // namespace Vkm::Engine
