#pragma once

#include <filesystem>
#include <fstream>
#include <system_error>

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

// Write @p doc to @p path, creating parent directories as needed. The dump goes
// to a sibling temp file that is renamed over the target only once the stream
// says it wrote cleanly, so a full disk or a crash mid-write leaves the previous
// file intact instead of a truncated one. Logs (tagged with @p what) and returns
// false on any failure; the caller must not report a save it did not get.
inline bool writeJsonFile(const std::filesystem::path& path, const nlohmann::json& doc, const char* what) {
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);

    const std::filesystem::path tmp = std::filesystem::path(path).concat(".tmp");
    {
        std::ofstream out(tmp);
        if (!out) {
            LOG_ERROR_C("IO", "%s: cannot open '%s' for writing", what, tmp.string().c_str());
            return false;
        }
        out << doc.dump(2);
        out.close();
        if (!out) {
            LOG_ERROR_C("IO", "%s: write to '%s' failed", what, tmp.string().c_str());
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        LOG_ERROR_C("IO", "%s: rename '%s' -> '%s' failed: %s", what,
            tmp.string().c_str(), path.string().c_str(), ec.message().c_str());
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace Engine::detail
