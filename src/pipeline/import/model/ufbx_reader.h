#pragma once

#include <memory>
#include <string>

#include <ufbx.h>

#include "import/model/source_model.h"

namespace Vkm::Engine::ModelReaders {

/// What every ufbx read of a file is told, so listing what a parse reads (filesReadBy) opens what it does.
ufbx_load_opts ufbxOptions();

/**
 * @brief Read an FBX or OBJ file with ufbx.
 *
 * @param path The file.
 * @param ref  What the log calls it.
 * @return The model; null, and an error logged, when it cannot be read.
 */
std::unique_ptr<SourceModel> parseUfbx(const std::string& path, const std::string& ref);

} // namespace Vkm::Engine::ModelReaders
