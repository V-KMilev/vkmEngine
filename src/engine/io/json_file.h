#pragma once

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "logger.h"

namespace Engine::detail {

// Open and parse a JSON file. On failure (cannot open / malformed JSON) logs an
// error tagged with @p what and returns false, leaving @p out untouched. Shared
// by the asset library, asset serializer, and scene serializer load paths.
//
// Uses the explicit-category log variant because this is an inline call in a
// header (see logger.h): the file's VKM_LOG_CATEGORY isn't reliably in scope, and
// every consumer lives in the "IO" subsystem.
inline bool readJsonFile(const std::filesystem::path& path, nlohmann::json& out, const char* what) {
    std::ifstream in(path);
    if (!in) {
        LOG_ERROR_C("IO", "%s: cannot open '%s'", what, path.string().c_str());
        return false;
    }
    try {
        in >> out;
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR_C("IO", "%s: malformed JSON in '%s': %s", what, path.string().c_str(), e.what());
        return false;
    }
    return true;
}

} // namespace Engine::detail
