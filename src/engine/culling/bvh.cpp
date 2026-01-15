#include "bvh.h"

#include <algorithm>
#include <limits>
#include <array>

namespace Engine {

static constexpr uint32_t MAX_PRIMS_PER_LEAF = 4;
static constexpr int SAH_BUCKETS = 12;
static constexpr size_t STACK_SIZE = 64;

void BVH::build(std::vector<BVHPrimitive> && primitives) {
    m_primitives = std::move(primitives);
    m_nodes.clear();

    if (m_primitives.empty()) {
        return;
    }

    // Reserve space for nodes (worst case: 2n-1 nodes for n primitives)
    m_nodes.reserve(2 * m_primitives.size());

    buildRecursive(0, static_cast<uint32_t>(m_primitives.size()));
}

void BVH::clear() {
    m_nodes.clear();
    m_primitives.clear();
}

uint32_t BVH::buildRecursive(uint32_t start, uint32_t end) {
    uint32_t nodeIdx = static_cast<uint32_t>(m_nodes.size());
    m_nodes.emplace_back();

    // Compute bounds for this node
    AABB bounds = computeBounds(start, end);
    m_nodes[nodeIdx].setBounds(bounds);

    const uint32_t primCount = end - start;

    // Create leaf if few primitives
    if (primCount <= MAX_PRIMS_PER_LEAF) {
        m_nodes[nodeIdx].setLeaf(start, primCount);
        return nodeIdx;
    }

    // Find best split using SAH
    int bestAxis;
    float bestSplitPos;
    float bestCost = findBestSplit(start, end, bestAxis, bestSplitPos);

    // If no good split found, create leaf
    const float leafCost = static_cast<float>(primCount);
    if (bestCost >= leafCost) {
        m_nodes[nodeIdx].setLeaf(start, primCount);
        return nodeIdx;
    }

    // Partition primitives
    auto mid = std::partition(
        m_primitives.begin() + start,
        m_primitives.begin() + end,
        [bestAxis, bestSplitPos](const BVHPrimitive& p) {
            return p.centroid[bestAxis] < bestSplitPos;
        }
    );

    uint32_t midIdx = static_cast<uint32_t>(mid - m_primitives.begin());

    // Handle degenerate case where partition didn't split
    if (midIdx == start || midIdx == end) {
        midIdx = (start + end) / 2;
        std::nth_element(
            m_primitives.begin() + start,
            m_primitives.begin() + midIdx,
            m_primitives.begin() + end,
            [bestAxis](const BVHPrimitive& a, const BVHPrimitive& b) {
                return a.centroid[bestAxis] < b.centroid[bestAxis];
            }
        );
    }

    // Recursively build children
    const uint32_t leftChild = buildRecursive(start, midIdx);
    const uint32_t rightChild = buildRecursive(midIdx, end);
    m_nodes[nodeIdx].setInternal(leftChild, rightChild);

    return nodeIdx;
}

float BVH::findBestSplit(uint32_t start, uint32_t end, int& axis, float& splitPos) const {
    float bestCost = std::numeric_limits<float>::max();
    axis = 0;
    splitPos = 0.0f;

    // Compute centroid bounds
    AABB centroidBounds;
    centroidBounds.min = glm::vec3(std::numeric_limits<float>::max());
    centroidBounds.max = glm::vec3(std::numeric_limits<float>::lowest());

    for (uint32_t i = start; i < end; ++i) {
        centroidBounds.min = glm::min(centroidBounds.min, m_primitives[i].centroid);
        centroidBounds.max = glm::max(centroidBounds.max, m_primitives[i].centroid);
    }

    // Try each axis
    for (int a = 0; a < 3; ++a) {
        const float axisMin = centroidBounds.min[a];
        const float axisMax = centroidBounds.max[a];

        if (axisMax - axisMin < 1e-6f) {
            continue;  // No extent on this axis
        }

        // Initialize buckets
        struct Bucket {
            uint32_t count = 0;
            AABB bounds;
            Bucket() {
                bounds.min = glm::vec3(std::numeric_limits<float>::max());
                bounds.max = glm::vec3(std::numeric_limits<float>::lowest());
            }
        };
        Bucket buckets[SAH_BUCKETS];

        // Populate buckets
        const float scale = static_cast<float>(SAH_BUCKETS) / (axisMax - axisMin);
        for (uint32_t i = start; i < end; ++i) {
            int b = static_cast<int>((m_primitives[i].centroid[a] - axisMin) * scale);
            b = std::clamp(b, 0, SAH_BUCKETS - 1);
            buckets[b].count++;
            buckets[b].bounds.expand(m_primitives[i].bounds);
        }

        // Compute costs for splitting after each bucket
        constexpr float TRAVERSAL_COST = 0.125f;

        AABB boundsLeft;
        boundsLeft.min = glm::vec3(std::numeric_limits<float>::max());
        boundsLeft.max = glm::vec3(std::numeric_limits<float>::lowest());
        uint32_t countLeft = 0;

        // Precompute right-side data
        AABB boundsRight[SAH_BUCKETS];
        uint32_t countRight[SAH_BUCKETS];

        AABB rightAccum;
        rightAccum.min = glm::vec3(std::numeric_limits<float>::max());
        rightAccum.max = glm::vec3(std::numeric_limits<float>::lowest());
        uint32_t rightCount = 0;

        for (int i = SAH_BUCKETS - 1; i >= 0; --i) {
            rightAccum.expand(buckets[i].bounds);
            rightCount += buckets[i].count;
            boundsRight[i] = rightAccum;
            countRight[i] = rightCount;
        }

        // Evaluate splits
        const float parentArea = computeBounds(start, end).surfaceArea();
        if (parentArea < 1e-6f) continue;

        for (int i = 0; i < SAH_BUCKETS - 1; ++i) {
            boundsLeft.expand(buckets[i].bounds);
            countLeft += buckets[i].count;

            if (countLeft == 0 || countRight[i + 1] == 0) continue;

            const float leftArea = boundsLeft.surfaceArea();
            const float rightArea = boundsRight[i + 1].surfaceArea();

            const float cost = TRAVERSAL_COST +
                (leftArea * countLeft + rightArea * countRight[i + 1]) / parentArea;

            if (cost < bestCost) {
                bestCost = cost;
                axis = a;
                splitPos = axisMin + (i + 1) * (axisMax - axisMin) / SAH_BUCKETS;
            }
        }
    }

    return bestCost;
}

AABB BVH::computeBounds(uint32_t start, uint32_t end) const {
    AABB bounds;
    bounds.min = glm::vec3(std::numeric_limits<float>::max());
    bounds.max = glm::vec3(std::numeric_limits<float>::lowest());

    for (uint32_t i = start; i < end; ++i) {
        bounds.expand(m_primitives[i].bounds);
    }

    return bounds;
}


std::vector<uint32_t> BVH::getVisibleInFrustum(const Frustum& frustum) const {
    std::vector<uint32_t> results;

    if (m_nodes.empty()) {
        return results;
    }

    results.reserve(m_primitives.size() / 4);
    queryIterative(frustum, results);

    return results;
}

void BVH::queryIterative(const Frustum& frustum, std::vector<uint32_t>& results) const {
    // Use explicit stack instead of recursion for better performance
    std::array<uint32_t, STACK_SIZE> stack;
    int stackPtr = 0;
    stack[stackPtr++] = 0;  // Start with root

    while (stackPtr > 0) {
        const uint32_t nodeIdx = stack[--stackPtr];
        const BVHNode& node = m_nodes[nodeIdx];

        // Test node bounds against frustum
        if (!isNodeVisible(node, frustum)) {
            continue;
        }

        if (node.isLeaf()) {
            // Test individual primitives
            const uint32_t first = node.firstPrim();
            const uint32_t count = node.primCount();
            for (uint32_t i = 0; i < count; ++i) {
                const auto& prim = m_primitives[first + i];
                if (FrustumCuller::isAABBVisible(frustum, prim.bounds.min, prim.bounds.max)) {
                    results.push_back(prim.entityId);
                }
            }
        } else {
            // Push children onto stack (right first so left is processed first)
            // Check if we have space for both children
            if (stackPtr + 1 < STACK_SIZE) {
                stack[stackPtr++] = node.rightChild();
                stack[stackPtr++] = node.leftChild();
            } else {
                // Stack overflow protection: if stack is full, skip remaining nodes
                // This should be rare with STACK_SIZE=64 for typical BVH depths
                continue;
            }
        }
    }
}

bool BVH::isNodeVisible(const BVHNode& node, const Frustum& frustum) const {
    // Inline AABB-frustum test for better performance
    const glm::vec3 nodeMin(node.minX, node.minY, node.minZ);
    const glm::vec3 nodeMax(node.maxX, node.maxY, node.maxZ);

    for (const auto& plane : frustum.planes) {
        const glm::vec3 normal(plane.x, plane.y, plane.z);

        // Find positive vertex (furthest along plane normal)
        glm::vec3 pVertex;
        pVertex.x = (normal.x >= 0.0f) ? nodeMax.x : nodeMin.x;
        pVertex.y = (normal.y >= 0.0f) ? nodeMax.y : nodeMin.y;
        pVertex.z = (normal.z >= 0.0f) ? nodeMax.z : nodeMin.z;

        if (glm::dot(normal, pVertex) + plane.w < 0.0f) {
            return false;
        }
    }

    return true;
}

std::vector<NodeBounds> BVH::getNodeBounds(int maxDepth, bool leavesOnly) const {
    std::vector<NodeBounds> results;

    if (m_nodes.empty()) {
        return results;
    }

    results.reserve(m_nodes.size());
    collectNodeBounds(0, 0, maxDepth, leavesOnly, results);

    return results;
}

void BVH::collectNodeBounds(uint32_t nodeIdx, int depth, int maxDepth, bool leavesOnly,
                            std::vector<NodeBounds>& results) const {
    if (nodeIdx >= m_nodes.size()) return;

    const BVHNode& node = m_nodes[nodeIdx];

    if (maxDepth >= 0 && depth > maxDepth) return;

    if (!leavesOnly || node.isLeaf()) {
        NodeBounds nb;
        nb.min = glm::vec3(node.minX, node.minY, node.minZ);
        nb.max = glm::vec3(node.maxX, node.maxY, node.maxZ);
        nb.depth = depth;
        nb.isLeaf = node.isLeaf();
        results.push_back(nb);
    }

    if (!node.isLeaf()) {
        collectNodeBounds(node.leftChild(), depth + 1, maxDepth, leavesOnly, results);
        collectNodeBounds(node.rightChild(), depth + 1, maxDepth, leavesOnly, results);
    }
}

} // namespace Engine
