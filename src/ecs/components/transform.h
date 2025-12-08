#pragma once

#include <memory>
#include <cstdint>

#include "component.h"

namespace Engine {
    class CPUTransform;
}

namespace Engine {

class Transform : public Component {
    public:
        Transform() = delete;
        ~Transform() override = default;

        Transform(const Transform& other) = delete;
        Transform& operator=(const Transform& other) = delete;

        Transform(Transform && other) = delete;
        Transform& operator=(Transform && other) = delete;

        Transform(uint32_t id);

    public:
        void setTransform(std::shared_ptr<CPUTransform> && transform);

        std::shared_ptr<CPUTransform>& getTransform() { return m_transform; }
        const std::shared_ptr<CPUTransform>& getTransform() const { return m_transform; }

    private:
        std::shared_ptr<CPUTransform> m_transform;
};

} // namespace Engine
