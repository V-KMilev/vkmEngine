#pragma once

#include <cstdint>

#include "component.h"

#include "resource_handle.h"

namespace Engine {

/**
 * @brief Component representing a renderable mesh (geometry + material) in the world.
 *
 * Holds references to mesh and material data, as well as flags for visibility and shadow casting.
 */
class Mesh final : public Component {
    public:
        Mesh() = delete;
        ~Mesh() override = default;

        /**
         * @brief Construct a Mesh component.
         * @param id           Unique component identifier.
         * @param mesh         Handle to mesh geometry.
         * @param material     Handle to material.
         * @param visible      Is mesh visible? (default: true)
         */
        Mesh(
            uint32_t id,
            MeshHandle mesh,
            MaterialHandle material,
            bool visible = true
        );

    public:
        /**
         * @brief Get the handle to the mesh geometry.
         * @return Reference to MeshHandle.
         */
        const MeshHandle& getMesh() const { return m_mesh; }

        /**
         * @brief Get the handle to the material.
         * @return Reference to MaterialHandle.
         */
        const MaterialHandle& getMaterial() const { return m_material; }

        /**
         * @brief Query if this mesh is visible.
         * @return True if visible, false if hidden.
         */
        bool isVisible() const { return m_visible; }

    private:
        MeshHandle m_mesh;
        MaterialHandle m_material;

        bool m_visible;
};

} // namespace Engine
