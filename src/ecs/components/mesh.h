#pragma once

#include <memory>
#include <cstdint>

#include "component.h"

namespace Engine {
    class CPUMesh;
    class CPUMaterial;
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

        Mesh(uint32_t id);

    public:
        void setMesh(std::shared_ptr<CPUMesh> && mesh);
        void setMaterial(std::shared_ptr<CPUMaterial> && material);

        std::shared_ptr<CPUMesh>& getMesh() { return m_mesh; }
        const std::shared_ptr<CPUMesh>& getMesh() const { return m_mesh; }

        std::shared_ptr<CPUMaterial>& getMaterial() { return m_material; }
        const std::shared_ptr<CPUMaterial>& getMaterial() const { return m_material; }

    private:
        std::shared_ptr<CPUMesh> m_mesh;
        std::shared_ptr<CPUMaterial> m_material;
};

} // namespace Engine
