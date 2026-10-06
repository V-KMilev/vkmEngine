#pragma once

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

#include "logger.h"

namespace Vkm::Engine::detail {

/**
 * @brief Open and parse a JSON file.
 *
 * Logs as IO explicitly: a header cannot rely on the includer's VKM_LOG_CATEGORY.
 *
 * @param path The file to read.
 * @param out  Receives the document; untouched on failure.
 * @param what What the document is, for the log line.
 * @return False, having logged, when the file will not open or parse.
 */
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

/**
 * @brief Replace every non-finite number under @p node with 0, logging each.
 *
 * nlohmann::json dumps inf and NaN as `null`, which the loaders throw on, so the
 * check is on write. Reads stay strict so `null` never becomes a legal scalar.
 *
 * @param node The subtree to walk.
 * @param what What the document is, for the log line.
 * @param path Scratch for the field's address; holds @p node's.
 */
inline void writeNonFiniteAsZero(nlohmann::json& node, const char* what, std::string& path) {
    if (node.is_object()) {
        const std::size_t parent = path.size();
        for (auto& field : node.items()) {
            path += '/';
            path += field.key();
            writeNonFiniteAsZero(field.value(), what, path);
            path.resize(parent);
        }
        return;
    }
    if (node.is_array()) {
        const std::size_t parent = path.size();
        for (std::size_t i = 0; i < node.size(); ++i) {
            path += '/';
            path += std::to_string(i);
            writeNonFiniteAsZero(node[i], what, path);
            path.resize(parent);
        }
        return;
    }
    if (!node.is_number_float() || std::isfinite(node.get<double>())) return;

    LOG_WARNING_C("IO", "%s: %s is not a finite number; written as 0", what, path.c_str());
    node = 0.0;
}

/**
 * @brief Hold @p doc to the rule above, from its root.
 *
 * @param doc  The document to walk.
 * @param what What the document is, for the log line.
 */
inline void writeNonFiniteAsZero(nlohmann::json& doc, const char* what) {
    std::string path;
    writeNonFiniteAsZero(doc, what, path);
}

/**
 * @brief Write @p doc to @p path, creating parent directories as needed.
 *
 * Writes a sibling temp file and renames it over the target, so a failed write
 * leaves the previous file intact.
 *
 * @param path The file to write.
 * @param doc  The document; mutable because non-finite numbers are zeroed.
 * @param what What the document is, for the log line.
 * @return False, having logged, on any failure.
 */
inline bool writeJsonFile(const std::filesystem::path& path, nlohmann::json& doc, const char* what) {
    writeNonFiniteAsZero(doc, what);

    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);

    const std::filesystem::path tmp = std::filesystem::path(path).concat(".tmp");
    {
        std::ofstream out(tmp);
        if (!out) {
            LOG_ERROR_C("IO", "%s: cannot open '%s' for writing", what, tmp.string().c_str());
            return false;
        }
        // Trailing newline, or every diff reports "no newline at end of file".
        out << doc.dump(2) << '\n';
        out.close();
        if (!out) {
            LOG_ERROR_C("IO", "%s: write to '%s' failed", what, tmp.string().c_str());
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        LOG_ERROR_C(
            "IO",
            "%s: rename '%s' -> '%s' failed: %s",
            what,
            tmp.string().c_str(),
            path.string().c_str(),
            ec.message().c_str()
        );
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace Vkm::Engine::detail
