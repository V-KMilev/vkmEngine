#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "vkm_network/bit_stream.h"

namespace Vkm::Engine {

constexpr size_t NET_PROJECT_PATH_MAX = 200;  ///< Longest project path the wire carries: a NetSpawn's prefab,
                                              ///< a World message's scene. Bounded because it arrives from
                                              ///< the network and opens a file; see isSayable.

/**
 * @brief Whether @p path can only name a JSON file inside the project.
 *
 * Relative, ".json", at most NET_PROJECT_PATH_MAX, no ".." component, backslash, colon,
 * control character or Windows device name - judged on characters, not std::filesystem, so
 * platforms agree: Linux would read \\host\share as relative. What a client opens on a
 * server's say-so - a spawned prefab, a world to load - must be such a path.
 *
 * @param path The path to judge.
 * @return True when it can name nothing outside the project.
 */
bool isProjectJsonPath(std::string_view path);

/**
 * @brief A project path as the wire carries it: its length, then its bytes.
 *
 * What isProjectJsonPath refuses goes as an empty path: a length that wrapped would misalign
 * everything after it.
 *
 * @param path The path to write.
 * @param out  The stream to write it to.
 */
void writeProjectPath(std::string_view path, Network::BitWriter& out);

/**
 * @brief A path writeProjectPath wrote, every byte read so the stream stays in step.
 *
 * Unjudged: whoever opens it asks isProjectJsonPath.
 *
 * @param in The stream to read it from.
 * @return The path; empty or partial when the stream ran out (in.failed()).
 */
std::string readProjectPath(Network::BitReader& in);

} // namespace Vkm::Engine
