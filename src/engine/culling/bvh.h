#pragma once

#include <vector>
#include <cstdint>
#include <array>

#include <glm/glm.hpp>

#ifdef __SSE2__
#include <emmintrin.h>
#endif

#include "frustum_culler.h"

namespace Engine {

/**
 * @brief Axis-aligned bounding box for BVH nodes.
 */
struct AABB {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    AABB() = default;
    AABB(const glm::vec3& min, const glm::vec3& max) : min(min), max(max) {}

    /**
     * @brief Expand this AABB to include another AABB.
     */
    void expand(const AABB& other) {
        min = glm::min(min, other.min);
        max = glm::max(max, other.max);
    }

    /**
     * @brief Get the center of the AABB.
     */
    glm::vec3 center() const { return (min + max) * 0.5f; }

    /**
     * @brief Get the surface area of the AABB (for SAH).
     */
    float surfaceArea() const {
        glm::vec3 d = max - min;
        return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
    }

    /**
     * @brief Get the longest axis (0=x, 1=y, 2=z).
     */
    int longestAxis() const {
        glm::vec3 d = max - min;
        if (d.x > d.y && d.x > d.z) return 0;
        if (d.y > d.z) return 1;
        return 2;
    }
};

/**
 * @brief Entity data for BVH construction.
 */
struct BVHPrimitive {
    uint32_t entityId;
    AABB bounds;
    glm::vec3 centroid;
};

/**
 * @brief Cache-friendly BVH node layout (32 bytes).
 *
 * Optimized for cache efficiency:
 * - Bounds stored as 6 floats (24 bytes)
 * - Child/primitive data packed (8 bytes)
 * - Total: 32 bytes = half a cache line
 */
struct alignas(32) BVHNode {
    // Bounds (24 bytes)
    float minX, minY, minZ;
    float maxX, maxY, maxZ;

    // For internal nodes: leftChild, rightChild
    // For leaf nodes: firstPrim, primCount
    uint32_t data0 = 0;
    uint32_t data1 = 0;

    bool isLeaf() const { return (data1 & 0x80000000u) != 0; }

    // Internal node accessors
    uint32_t leftChild() const { return data0; }
    uint32_t rightChild() const { return data1 & 0x7FFFFFFFu; }

    // Leaf node accessors
    uint32_t firstPrim() const { return data0; }
    uint32_t primCount() const { return data1 & 0x7FFFFFFFu; }

    void setInternal(uint32_t left, uint32_t right) {
        data0 = left;
        data1 = right & 0x7FFFFFFFu;  // Clear leaf flag
    }

    void setLeaf(uint32_t first, uint32_t count) {
        data0 = first;
        data1 = (count & 0x7FFFFFFFu) | 0x80000000u;  // Set leaf flag
    }

    void setBounds(const AABB& b) {
        minX = b.min.x; minY = b.min.y; minZ = b.min.z;
        maxX = b.max.x; maxY = b.max.y; maxZ = b.max.z;
    }

    AABB getBounds() const {
        return AABB(glm::vec3(minX, minY, minZ), glm::vec3(maxX, maxY, maxZ));
    }
};

/**
 * @brief Bounding Volume Hierarchy for efficient frustum culling.
 *
 * Uses Surface Area Heuristic (SAH) for optimal tree construction.
 * Provides O(log n) query performance for frustum culling.
 */
class BVH {
public:
    BVH() = default;
    ~BVH() = default;

    /**
     * @brief Build the BVH from a list of primitives.
     * @param primitives Entity bounds and IDs to build from.
     */
    void build(std::vector<BVHPrimitive> primitives);

    /**
     * @brief Clear the BVH.
     */
    void clear();

    /**
     * @brief Query all entity IDs visible within the frustum.
     * @param frustum The view frustum to test against.
     * @return Vector of visible entity IDs.
     */
    std::vector<uint32_t> queryFrustum(const Frustum& frustum) const;

    /**
     * @brief Check if the BVH is empty.
     */
    bool empty() const { return m_nodes.empty(); }

    /**
     * @brief Get the number of nodes in the BVH.
     */
    size_t getNodeCount() const { return m_nodes.size(); }

    /**
     * @brief Get the number of primitives in the BVH.
     */
    size_t getPrimitiveCount() const { return m_primitives.size(); }

    /**
     * @brief Get root node bounds for early-out tests.
     */
    glm::vec3 getRootMin() const { return m_nodes.empty() ? glm::vec3(0) : glm::vec3(m_nodes[0].minX, m_nodes[0].minY, m_nodes[0].minZ); }
    glm::vec3 getRootMax() const { return m_nodes.empty() ? glm::vec3(0) : glm::vec3(m_nodes[0].maxX, m_nodes[0].maxY, m_nodes[0].maxZ); }

    /**
     * @brief Get all node bounds for visualization.
     * @param maxDepth Maximum depth to return (-1 = all).
     * @param leavesOnly Only return leaf node bounds.
     * @return Vector of (min, max, depth) tuples.
     */
    struct NodeBounds {
        glm::vec3 min;
        glm::vec3 max;
        int depth;
        bool isLeaf;
    };
    std::vector<NodeBounds> getNodeBounds(int maxDepth = -1, bool leavesOnly = false) const;

private:
    void collectNodeBounds(uint32_t nodeIdx, int depth, int maxDepth, bool leavesOnly,
                           std::vector<NodeBounds>& results) const;
    /**
     * @brief Recursively build a subtree.
     * @param start Start index in primitives array.
     * @param end End index (exclusive) in primitives array.
     * @return Index of the created node.
     */
    uint32_t buildRecursive(uint32_t start, uint32_t end);

    /**
     * @brief Find the best split using SAH.
     * @param start Start index.
     * @param end End index.
     * @param axis Output: best axis to split on.
     * @param splitPos Output: position to split at.
     * @return Cost of the best split (FLT_MAX if no good split found).
     */
    float findBestSplit(uint32_t start, uint32_t end, int& axis, float& splitPos) const;

    /**
     * @brief Compute bounds for a range of primitives.
     */
    AABB computeBounds(uint32_t start, uint32_t end) const;

    /**
     * @brief Iterative query using explicit stack (faster than recursion).
     */
    void queryIterative(const Frustum& frustum, std::vector<uint32_t>& results) const;

    /**
     * @brief Fast AABB-frustum test using node's packed bounds.
     */
    bool isNodeVisible(const BVHNode& node, const Frustum& frustum) const;

#ifdef __SSE2__
    /**
     * @brief SIMD-optimized frustum test for 4 planes at once.
     */
    bool isNodeVisibleSIMD(const BVHNode& node, const Frustum& frustum) const;
#endif

    std::vector<BVHNode> m_nodes;
    std::vector<BVHPrimitive> m_primitives;

    static constexpr uint32_t MAX_PRIMS_PER_LEAF = 4;
    static constexpr int SAH_BUCKETS = 12;
    static constexpr size_t STACK_SIZE = 64;  // Max tree depth
};

} // namespace Engine

