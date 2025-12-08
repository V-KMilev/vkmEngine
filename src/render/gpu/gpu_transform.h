#pragma once

namespace Core {
    class Shader;
}

namespace Engine {
    class CPUTransform;
}

namespace Engine {

/**
 * @class GPUTransform
 * @brief Handles a reference to a CPUTransform and uploads transform data to GPU shaders.
 *
 * The GPUTransform class holds a reference to a CPUTransform object and provides methods
 * to update and upload the transform data to the GPU using a given shader. Copy and move operations
 * are explicitly disabled to avoid accidental sharing or ownership issues.
 */
class GPUTransform {
    public:
        GPUTransform() = delete;
        ~GPUTransform() = default;

        GPUTransform(const GPUTransform& other) = delete;
        GPUTransform& operator=(const GPUTransform& other) = delete;

        GPUTransform(GPUTransform && other) = delete;
        GPUTransform& operator=(GPUTransform && other) = delete;

        explicit GPUTransform(const CPUTransform& cpuTransform);

    public:
        /**
         * @brief Sets the source CPUTransform for this GPUTransform.
         * @param cpuTransform Reference to the CPUTransform to use as source.
         */
        void setSource(const CPUTransform& cpuTransform);

        /**
         * @brief Uploads the transformation data to the provided Shader.
         * @param shader Reference to the Shader object into which to upload the transform data.
         */
        void upload(const Core::Shader& shader) const;

    private:
        const CPUTransform* m_source;
};

} // namespace Engine