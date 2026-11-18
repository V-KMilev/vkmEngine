#pragma once

#include <filesystem>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "vkm_log/logger.h"

#include "io/json/json_file.h"

namespace Vkm::Engine {

/**
 * @brief A project's own file - its settings, a player's profile, a save - as JSON.
 *
 * None written yet is the usual first run, so an empty object, and quietly; one that will not
 * parse, or holds no object, is an empty object too, with a warning, so a damaged settings file
 * costs the player their settings rather than the game.
 *
 * @param path The file, under Roots::playerRoot.
 * @return Its document, or an empty object.
 */
inline nlohmann::json readOwnFile(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return nlohmann::json::object();
    nlohmann::json doc;
    std::ifstream  in(path);
    try {
        in >> doc;
    } catch (const nlohmann::json::exception& e) {
        LOG_WARNING_C("IO", "'%s' cannot be read (%s); starting afresh", path.string().c_str(), e.what());
        return nlohmann::json::object();
    }
    if (!doc.is_object()) {
        LOG_WARNING_C("IO", "'%s' holds no object; starting afresh", path.string().c_str());
        return nlohmann::json::object();
    }
    return doc;
}

/**
 * @brief @p doc to a project's own file at @p path, whole or not at all.
 *
 * @param path The file, under Roots::playerRoot; its folders are made.
 * @param doc  The document.
 * @return False, having logged, when it could not be written; the file before is left as it was.
 */
inline bool writeOwnFile(const std::filesystem::path& path, nlohmann::json doc) {
    return detail::writeJsonFile(path, doc, "project file");
}

} // namespace Vkm::Engine
