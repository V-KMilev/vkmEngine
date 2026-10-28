#pragma once

#include <filesystem>

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Write each terrain whose .terrain file differs from it, and name the file its source.
 *
 * A terrain the brushes changed lives only in memory until this; the cook reads the file.
 *
 * @param resources The terrains.
 * @return False when a file could not be written, having logged.
 */
bool writeTerrainFiles(ResourceManager& resources);

/**
 * @brief Put each terrain back as its .terrain file holds it, where the two differ.
 *
 * Stop's half of what Play's writeTerrainFiles began: what a session dug or brushed goes, in place,
 * so handles held keep naming each terrain. A terrain with no file, or an unreadable one, is left
 * as it is.
 *
 * @param resources The terrains.
 */
void readTerrainFiles(ResourceManager& resources);

/**
 * @brief Keep, under @p dir, each terrain whose .terrain file differs from it: a crash copy's.
 *
 * Its project file is left as it is: unsaved changes are the author's to save. What @p dir held
 * before is replaced.
 *
 * @param resources The terrains.
 * @param dir       Where the bodies go, one a terrain, named by a hash of its name.
 * @return False when a body could not be written.
 */
bool keepTerrainCopies(const ResourceManager& resources, const std::filesystem::path& dir);

/**
 * @brief Put each terrain back as its body under @p dir holds it, where one is kept.
 *
 * keepTerrainCopies' other half, after a crash copy loads: in place, so handles keep naming each.
 *
 * @param resources The terrains.
 * @param dir       Where keepTerrainCopies kept them.
 */
void readTerrainCopies(ResourceManager& resources, const std::filesystem::path& dir);

} // namespace Vkm::Engine
