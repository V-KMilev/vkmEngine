#pragma once

#include <vector>
#include <cstdint>
#include <array>

#include <glm/glm.hpp>

#include "entity.h"
#include "frustum_culler.h"

namespace Engine {

/**
 * @brief Axis-Aligned Bounding Box used for BVH construction and queries.
 *
 * Represents a box aligned to the world axes, for efficient intersection tests.
 */
struct AABB {
    glm::vec3 min{0.0f}; ///< Minimum corner (lower x, y, z)
    glm::vec3 max{0.0f}; ///< Maximum corner (upper x, y, z)

    AABB() = default;
    AABB(const glm::vec3& min, const glm::vec3& max) : min(min), max(max) {}

    /**
     * @brief Expands this bounding box to encapsulate another AABB.
     * @param other The AABB to encompass.
     */
    void expand(const AABB& other) {
        min = glm::min(min, other.min);
        max = glm::max(max, other.max);
    }

    /**
     * @brief Returns the center point of the box.
     * @return Centroid of AABB.
     */
    glm::vec3 center() const { return (min + max) * 0.5f; }

    /**
     * @brief Returns the surface area of the box.
     * @return Surface area (used for SAH).
     */
    float surfaceArea() const {
        glm::vec3 d = max - min;
        return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
    }

    /**
     * @brief Returns the axis along which the box is longest (0=x, 1=y, 2=z).
     */
    int longestAxis() const {
        glm::vec3 d = max - min;
        if (d.x > d.y && d.x > d.z) return 0;
        if (d.y > d.z) return 1;
        return 2;
    }
};

/**
 * @brief Primitive stored in the BVH. Links an entity to its bounds and centroid.
 */
struct BVHPrimitive {
    EntityId entityId;     ///< Entity's unique identifier.
    AABB bounds;           ///< Bounding box (in world space).
    glm::vec3 centroid;    ///< Centroid of the bounds (used for splitting).
};

/**
 * @brief Holds metadata and bounds for a BVH node, for debug/visualization.
 */
struct NodeBounds {
    glm::vec3 min;    ///< Minimum AABB bounds for the node.
    glm::vec3 max;    ///< Maximum AABB bounds for the node.
    int depth;        ///< Depth in the tree (root = 0).
    bool isLeaf;      ///< True if leaf node, otherwise false.
};

/**
 * @brief Node structure in the BVH. Optimized for SIMD and cache efficiency.
 *
 * Child indices and primitive ranges are stored in unions for compactness.
 *
 * The node is 32-byte aligned for better SIMD performance.
 */
struct alignas(32) BVHNode {
    // Bounds (24 bytes: 2x vec3)
    float minX, minY, minZ; ///< Minimum x, y, z of bounds.
    float maxX, maxY, maxZ; ///< Maximum x, y, z of bounds.

    // For internal nodes: leftChild, rightChild
    // For leaf nodes: firstPrim, primCount
    uint32_t data0 = 0; ///< Left child or first primitive index.
    uint32_t data1 = 0; ///< Right child or primitive count (high bit signals leaf).

    /**
     * @brief Returns true if this node is a leaf (contains primitives).
     */
    bool isLeaf() const { return (data1 & 0x80000000u) != 0; }

    // Internal node accessors

    /**
     * @brief Gets the left child node index (internal nodes).
     */
    uint32_t leftChild() const { return data0; }

    /**
     * @brief Gets the right child node index (internal nodes).
     */
    uint32_t rightChild() const { return data1 & 0x7FFFFFFFu; }

    // Leaf node accessors

    /**
     * @brief Gets the index of the first primitive (leaf nodes).
     */
    uint32_t firstPrim() const { return data0; }

    /**
     * @brief Gets the number of primitives in the leaf (leaf nodes).
     */
    uint32_t primCount() const { return data1 & 0x7FFFFFFFu; }

    /**
     * @brief Configures this node as an internal node with specified children.
     * @param left Index of left child node.
     * @param right Index of right child node.
     */
    void setInternal(uint32_t left, uint32_t right) {
        data0 = left;
        data1 = right & 0x7FFFFFFFu;  // Clear leaf flag
    }

    /**
     * @brief Configures this node as a leaf node with primitives.
     * @param first Index of first primitive.
     * @param count Number of primitives in this leaf node.
     */
    void setLeaf(uint32_t first, uint32_t count) {
        data0 = first;
        data1 = (count & 0x7FFFFFFFu) | 0x80000000u;  // Set leaf flag
    }

    /**
     * @brief Assigns bounds for this node.
     * @param b Bounds to set.
     */
    void setBounds(const AABB& b) {
        minX = b.min.x; minY = b.min.y; minZ = b.min.z;
        maxX = b.max.x; maxY = b.max.y; maxZ = b.max.z;
    }

    /**
     * @brief Returns this node's bounds as an AABB.
     * @return Axis-aligned bounding box for the node.
     */
    AABB getBounds() const {
        return AABB(glm::vec3(minX, minY, minZ), glm::vec3(maxX, maxY, maxZ));
    }
};

/**
 * @brief Bounding Volume Hierarchy structure for fast spatial queries and culling.
 *
 * The BVH accelerates view-frustum and intersection queries by hierarchically
 * partitioning space using AABBs over a set of primitives. Provides methods for
 * construction, querying, and debug/visualization inspection.
 *
 * Not thread-safe. Rebuild or modify only from one thread at a time.
 */
class BVH {
    public:
        BVH() = default;
        ~BVH() = default;

        BVH(const BVH& other) = delete;
        BVH& operator=(const BVH& other) = delete;

        BVH(BVH && other) = delete;
        BVH& operator=(BVH && other) = delete;

    public:
        /**
         * @brief Builds the BVH from a vector of primitives.
         * Consumes ownership of the primitives vector.
         * @param primitives List of primitives to build from (will be moved-from).
         */
        void build(std::vector<BVHPrimitive> && primitives);

        /**
         * @brief Clears all BVH nodes and primitives.
         */
        void clear();

        /**
         * @brief Returns a list of entity IDs visible in the view frustum.
         * @param frustum The frustum to test against.
         * @return Entity IDs of primitives that are visible in the frustum.
         */
        std::vector<uint32_t> getVisibleInFrustum(const Frustum& frustum) const;

        /**
         * @brief Collects the bounds and metadata of all nodes for visualization or debug.
         * @param maxDepth Optional maximum tree depth to capture (-1 = unlimited).
         * @param leavesOnly If true, only records leaves; otherwise all nodes.
         * @return List of NodeBounds objects.
         */
        std::vector<NodeBounds> getNodeBounds(
            int maxDepth = -1,
            bool leavesOnly = false
        ) const;

        /**
         * @brief Returns true if the BVH is empty (contains no nodes).
         */
        bool empty() const { return m_nodes.empty(); }

        /**
         * @brief Returns the current number of nodes in the BVH.
         */
        size_t getNodeCount() const { return m_nodes.size(); }

        /**
         * @brief Returns the number of primitives contained in the BVH.
         */
        size_t getPrimitiveCount() const { return m_primitives.size(); }

        /**
         * @brief Gets the minimum (x, y, z) of the BVH root bounds.
         * Returns (0,0,0) if the BVH is empty.
         */
        glm::vec3 getRootMin() const { return m_nodes.empty() ? glm::vec3(0) : glm::vec3(m_nodes[0].minX, m_nodes[0].minY, m_nodes[0].minZ); }

        /**
         * @brief Gets the maximum (x, y, z) of the BVH root bounds.
         * Returns (0,0,0) if the BVH is empty.
         */
        glm::vec3 getRootMax() const { return m_nodes.empty() ? glm::vec3(0) : glm::vec3(m_nodes[0].maxX, m_nodes[0].maxY, m_nodes[0].maxZ); }

    private:
        /**
         * @brief Helper: Recursively (or iteratively) collects bounds for BVH nodes.
         * @param nodeIdx Start node index.
         * @param depth Current node depth.
         * @param maxDepth Maximum allowed depth (-1 = unlimited).
         * @param leavesOnly Collect only leaves.
         * @param results Output vector for NodeBounds.
         */
        void collectNodeBounds(
            uint32_t nodeIdx,
            int depth,
            int maxDepth,
            bool leavesOnly,
            std::vector<NodeBounds>& results
        ) const;

        /**
         * @brief Internal recursive build (called from build()).
         * @param start Primitive begin index.
         * @param end Primitive end index.
         * @return Index of constructed node.
         */
        uint32_t buildRecursive(
            uint32_t start,
            uint32_t end
        );

        /**
         * @brief Finds the best split axis and position using surface area heuristic.
         * @param start Primitive begin index.
         * @param end Primitive end index.
         * @param axis Output axis of best split.
         * @param splitPos Output position along axis.
         * @return SAH cost of the split.
         */
        float findBestSplit(uint32_t start, uint32_t end, int& axis, float& splitPos) const;

        /**
         * @brief Computes a bounding box for primitives in range [start, end).
         */
        AABB computeBounds(uint32_t start, uint32_t end) const;

        /**
         * @brief Iteratively traverses the BVH against the frustum. Fills result vector.
         * @param frustum Frustum used for testing.
         * @param results Index list to append visible primitives to.
         */
        void queryIterative(const Frustum& frustum, std::vector<uint32_t>& results) const;

        /**
         * @brief Tests if a node's bounding box is visible in the frustum.
         */
        bool isNodeVisible(const BVHNode& node, const Frustum& frustum) const;

    private:
        std::vector<BVHNode> m_nodes;
        std::vector<BVHPrimitive> m_primitives;
};

} // namespace Engine
