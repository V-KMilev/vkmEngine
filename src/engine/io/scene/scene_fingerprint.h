#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "core/fnv1a.h"

namespace Vkm::Engine {

/**
 * @brief A hash of the scene file at @p path, line ends aside, or zero when there is no file.
 *
 * Both ends of a game must load the same world, as a slot is an entity's name on the wire; this
 * catches a stale scene. Zero (no file behind the world) compares equal to nothing, so such a host
 * neither offers nor demands agreement.
 *
 * @param path Absolute path of the world's scene file: the entry scene, or one loaded later.
 * @return The hash, or zero.
 */
inline uint64_t fingerprintScene(const std::filesystem::path& path) {
    if (path.empty()) return 0;

    std::ifstream in(path, std::ios::binary);
    if (!in) return 0;

    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return 0;

    // Without '\r': a Windows checkout may hold CRLF, and in JSON '\r' is only whitespace.
    bytes.erase(std::remove(bytes.begin(), bytes.end(), '\r'), bytes.end());

    const uint64_t hash = fnv1a64Bytes(bytes.data(), bytes.size());
    return hash == 0 ? 1 : hash;  // zero is reserved for "no file"
}

} // namespace Vkm::Engine
