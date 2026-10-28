#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Vkm::Engine {

/**
 * @brief The content the engine ships for a project to take in: packs laid out as a project is.
 *
 * A pack under ProjectPaths::engineContent() holds what a project would - assets/, prefabs/ and
 * the recipes in library/ - and is copied into one, never read in place. The project then owns its
 * copy, as it owns everything else it cooks and ships.
 */
namespace ContentPack {

/**
 * @brief The engine's pack of @p name.
 *
 * @param name Its folder under ProjectPaths::engineContent(), as "effects".
 * @return Where it is.
 */
std::filesystem::path shipped(const std::string& name);

/**
 * @brief The prefabs @p pack holds in its prefabs/@p folder.
 *
 * @param pack   A pack.
 * @param folder The folder under its prefabs/.
 * @return Each one's file name, in name order; none when the folder is not there.
 */
std::vector<std::string> prefabsIn(const std::filesystem::path& pack, const std::string& folder);

/**
 * @brief Copy each file of @p pack that @p project lacks, at the same path below it.
 *
 * A file the project has is left as it is, edits and all.
 *
 * @param pack    The pack.
 * @param project The project's root.
 * @param error   Set to what failed, on a failure.
 * @return How many files were copied, or -1 on a failure.
 */
int bringInto(const std::filesystem::path& pack, const std::filesystem::path& project, std::string& error);

} // namespace ContentPack
} // namespace Vkm::Engine
