#include "cpu_material.h"

namespace Engine {
    CPUMaterial::CPUMaterial(
        const MaterialProperties& properties
    ) : m_properties(properties) {}

    void CPUMaterial::setProperties(const MaterialProperties& properties) {
        m_properties = properties;
    }

    const MaterialProperties& CPUMaterial::getProperties() const {
        return m_properties;
    }

    bool CPUMaterial::loadFromFile(const std::string& filePath) {
        // TODO: Implement material loading from file (e.g., JSON, MTL)
        (void)filePath;
        return false;
    }
} // namespace Engine
