#pragma once

#include <memory>
#include <string>

#include "import/model/source_model.h"

namespace Vkm::Engine::ModelReaders {

/**
 * @brief Read a glTF or GLB file with cgltf.
 *
 * @param path The file.
 * @param ref  What the log calls it.
 * @return The model; null, and an error logged, when it cannot be read.
 */
std::unique_ptr<SourceModel> parseGltf(const std::string& path, const std::string& ref);

} // namespace Vkm::Engine::ModelReaders
