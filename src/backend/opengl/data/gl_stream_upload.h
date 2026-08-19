#pragma once

#include <cstdint>
#include <memory>

#include <GL/glew.h>

namespace Vkm::Engine {

/**
 * @brief Grow-or-update a stream buffer to hold @p bytes of @p data.
 *
 * The per-frame GPU buffers size to whatever the frame holds - instance counts,
 * bone counts - and that moves every frame. Reallocating only when it grows,
 * with headroom, keeps a steady scene at one allocation and a growing one at a
 * handful.
 *
 * @tparam BufferT   vkmGL buffer type to allocate (vertex or shader storage).
 * @param buffer     The buffer, allocated on first use and on every grow.
 * @param capacity   Bytes currently allocated; updated when it grows.
 * @param data       Source bytes to upload.
 * @param bytes      How many of them; 0 leaves the buffer untouched.
 */
template <typename BufferT>
void growAndUpload(std::unique_ptr<BufferT>& buffer, uint32_t& capacity,
                   const void* data, uint32_t bytes) {
    if (bytes == 0) return;
    if (!buffer || bytes > capacity) {
        capacity = bytes + bytes / 2;   // headroom, so a growing frame does not realloc every time
        buffer = std::make_unique<BufferT>(nullptr, capacity, GL_STREAM_DRAW);
    }
    buffer->update(data, bytes, 0);
}

} // namespace Vkm::Engine
