#pragma once

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

} // namespace Vkm::Engine
