#pragma once

#include <cstring>
#include <memory>

#include <GL/glew.h>

#include "gl_uniform_buffer.h"

namespace Engine {

/**
 * @brief Upload @p data into @p ubo only when it differs from the cached @p last.
 *
 * Creates the buffer on first use, otherwise re-uploads only when the POD struct
 * changed (memcmp) - so a static camera / light set costs no GPU write per frame.
 */
template <typename T>
inline void uploadUBOIfChanged(std::unique_ptr<Core::UniformBuffer>& ubo, T& last, const T& data) {
    const bool first = !ubo;
    if (!first && std::memcmp(&data, &last, sizeof(T)) == 0) return;
    if (first) ubo = std::make_unique<Core::UniformBuffer>(&data, sizeof(T), GL_DYNAMIC_DRAW);
    else       ubo->update(&data, sizeof(T));
    last = data;
}

/// Bind @p ubo to @p bindingPoint when it exists.
inline void bindUBO(const std::unique_ptr<Core::UniformBuffer>& ubo, uint32_t bindingPoint) {
    if (ubo) ubo->bindBase(bindingPoint);
}

} // namespace Engine
