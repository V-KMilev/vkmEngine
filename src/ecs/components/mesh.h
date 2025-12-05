#pragma once

#include <memory>
#include <cstdint>

#include "component.h"

namespace Engine {
    class GPUMesh;
    class GPUMaterial;
}

namespace Engine {

class Mesh : public Component {
    public:
        Mesh() = delete;
        ~Mesh() override = default;

        Mesh(const Mesh& other) = delete;
        Mesh& operator=(const Mesh& other) = delete;

        Mesh(Mesh&& other) = delete;
        Mesh& operator=(Mesh&& other) = delete;

        Mesh(uint32_t id, uint32_t entityId);

    public:
        void setMesh(std::shared_ptr<GPUMesh> && mesh);
        void setMaterial(std::shared_ptr<GPUMaterial> && material);

        std::shared_ptr<GPUMesh>& getMesh();
        const std::shared_ptr<GPUMesh>& getMesh() const;

        std::shared_ptr<GPUMaterial>& getMaterial();
        const std::shared_ptr<GPUMaterial>& getMaterial() const;


    private:
        std::shared_ptr<GPUMesh> m_mesh;
        std::shared_ptr<GPUMaterial> m_material;
    };

} // namespace Engine
