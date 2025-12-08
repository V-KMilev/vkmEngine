#pragma once

namespace Core {
    class Shader;
}

namespace Engine {
    class CPUCamera;
}

namespace Engine {

/**
 * @class GPUCamera
 * @brief Manages a reference to the CPUCamera and uploads camera data to GPU shaders.
 *
 * The GPUCamera class holds a reference to a CPUCamera object and provides methods
 * to update and upload the camera parameters (such as view and projection matrices) to
 * the GPU using a given shader. Copy and move operations are explicitly disabled to avoid
 * accidental sharing or ownership issues.
 */
class GPUCamera {
    public:
        GPUCamera() = delete;

        ~GPUCamera() = default;

        GPUCamera(const GPUCamera& other) = delete;
        GPUCamera& operator=(const GPUCamera& other) = delete;

        GPUCamera(GPUCamera && other) = delete;
        GPUCamera& operator=(GPUCamera && other) = delete;

        explicit GPUCamera(const CPUCamera& cpuCamera);

    public:
        /**
         * @brief Sets the source CPUCamera for this GPUCamera.
         * This allows the GPUCamera to reference updated camera data.
         * @param cpuCamera Reference to the CPUCamera to use as source.
         */
        void setSource(const CPUCamera& cpuCamera);

        /**
         * @brief Uploads the camera data (e.g., view and projection matrices) to the provided Shader.
         * @param shader Reference to the Shader object into which to upload the camera data.
         */
        void upload(const Core::Shader& shader) const;

    private:
        const CPUCamera* m_source;
};

} // namespace Engine

