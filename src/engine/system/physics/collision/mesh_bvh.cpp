#include "system/physics/collision/mesh_bvh.h"

#include <algorithm>
#include <array>
#include <limits>
#include <numeric>

namespace Vkm::Engine {

namespace {

// Below this a node is a leaf. Splitting further costs a node and a branch to
// save a handful of triangle tests, which the tests were cheaper than.
constexpr uint32_t LEAF_TRIANGLES = 4;

struct Bounds {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

Bounds boundsOf(const std::vector<glm::vec3>& triangles, uint32_t first,
                uint32_t count) {
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
    return (triangles[triangle * 3 + 0]
          + triangles[triangle * 3 + 1]
          + triangles[triangle * 3 + 2]) / 3.0f;
}

void swapTriangles(std::vector<glm::vec3>& triangles, uint32_t a, uint32_t b) {
    if (a == b) return;
    for (uint32_t c = 0; c < 3; ++c) {
        std::swap(triangles[a * 3 + c], triangles[b * 3 + c]);
    }
}

uint32_t buildNode(std::vector<glm::vec3>& triangles,
                   std::vector<MeshNode>& nodes,
                   uint32_t first, uint32_t count) {
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

    // Widest axis, median split. The alternative worth having is a surface-area
    // heuristic, which is a better tree and a longer build; this one is within
    // a constant factor and fits in a page.
    const glm::vec3 extent = bounds.max - bounds.min;
    int axis = 0;
    if (extent.y > extent[axis]) axis = 1;
    if (extent.z > extent[axis]) axis = 2;

    // Ordered through an index array rather than over the points themselves:
    // nth_element on raw points would tear triangles apart, since a triangle is
    // three of them and the algorithm knows nothing of that.
    std::vector<uint32_t> order(count);
    std::iota(order.begin(), order.end(), first);
    const auto middleIt = order.begin() + count / 2;
    std::nth_element(order.begin(), middleIt, order.end(),
        [&](uint32_t a, uint32_t b) {
            const glm::vec3 ca = centroidOf(triangles, a);
            const glm::vec3 cb = centroidOf(triangles, b);
            return ca[axis] < cb[axis];
        });

    // Apply the permutation. A triangle already in place is left alone, and
    // every swap puts at least one where it belongs, so this terminates.
    std::vector<uint32_t> slotOf(count);
    for (uint32_t i = 0; i < count; ++i) slotOf[order[i] - first] = i;
    for (uint32_t i = 0; i < count; ++i) {
        while (slotOf[i] != i) {
            const uint32_t target = slotOf[i];
            swapTriangles(triangles, first + i, first + target);
            std::swap(slotOf[i], slotOf[target]);
        }
    }

    const uint32_t leftCount = count / 2;
    buildNode(triangles, nodes, first, leftCount);
    const uint32_t right = buildNode(triangles, nodes, first + leftCount,
                                     count - leftCount);
    nodes[self].rightChild = right;
    nodes[self].triangleCount = 0;
    return self;
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

void queryMeshBvh(const std::vector<MeshNode>& nodes,
                  const glm::vec3& min, const glm::vec3& max,
                  std::vector<uint32_t>& out) {
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

        // Losing a branch loses contacts; running off the end of the stack
        // corrupts memory. So the near child is kept whenever there is room, and
        // only the far one is dropped.
        const uint32_t left = static_cast<uint32_t>(&node - nodes.data()) + 1;
        const int room = static_cast<int>(stack.size()) - top;
        if (room >= 2) stack[top++] = node.rightChild;
        if (room >= 1) stack[top++] = left;
    }
}

} // namespace Vkm::Engine
