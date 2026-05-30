#pragma once

#include <cstdint>
#include <cstring>
#include <memory>

#include <GL/glew.h>

#include "gl_uniform_buffer.h"

namespace Engine {

/**
 * @brief Upload @p data into @p ubo only when it differs from @p last.
 *
 * The per-frame UBO wrappers (GLCamera, GLLights) share this exact pattern:
 * skip the GPU upload when the POD UBO struct is byte-identical to the previous
 * frame (memcmp), creating the buffer on first use. @p usage is the first-time
 * creation hint - GLCamera uses GL_DYNAMIC_DRAW; GLLights keeps GL_STATIC_DRAW,
 * its historical (ctor-default) value.
 */
template <typename T>
inline void uploadUBOIfChanged(std::unique_ptr<Core::UniformBuffer>& ubo,
                               T& last, const T& data,
                               GLenum usage = GL_DYNAMIC_DRAW) {
    const bool firstUpload = !ubo;
    if (!firstUpload && std::memcmp(&data, &last, sizeof(T)) == 0) return;
    if (firstUpload) ubo = std::make_unique<Core::UniformBuffer>(&data, sizeof(T), usage);
    else             ubo->update(&data, sizeof(T));
    last = data;
}

/// Bind @p ubo to @p bindingPoint when it exists (shared by the UBO wrappers).
inline void bindUBO(const std::unique_ptr<Core::UniformBuffer>& ubo, uint32_t bindingPoint) {
    if (ubo) ubo->bindBase(bindingPoint);
}

} // namespace Engine
