#pragma once

#include <memory>
#include <cstdint>

#include "component.h"

namespace Engine {
    class CPUCamera;
}

namespace Engine {

class Camera : public Component {
    public:
        Camera() = delete;
        ~Camera() override = default;

        Camera(const Camera& other) = delete;
        Camera& operator=(const Camera& other) = delete;

        Camera(Camera&& other) = delete;
        Camera& operator=(Camera&& other) = delete;

        Camera(uint32_t id);

    public:
        void setCamera(std::shared_ptr<CPUCamera> && camera);

        std::shared_ptr<CPUCamera>& getCamera() { return m_camera; }
        const std::shared_ptr<CPUCamera>& getCamera() const { return m_camera; }

    private:
        std::shared_ptr<CPUCamera> m_camera;
};

} // namespace Engine