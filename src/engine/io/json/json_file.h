#pragma once

#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

#include "vkm_log/logger.h"

#include "io/json/json_number.h"
#include "io/whole_file_write.h"

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
    // Parsed aside: the parser builds into its target as it goes, so a throw part-way would
    // leave half a document.
    nlohmann::json read;
    try {
        in >> read;
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR_C("IO", "%s: malformed JSON in '%s': %s", what, path.string().c_str(), e.what());
        return false;
    }
    out = std::move(read);
    return true;
}

/**
 * @brief Whether @p doc carries a layout version this build reads: a positive whole number no newer
 *        than @p newest.
 *
 * The gate refuses the future, not the past: an older file is read by the current reader.
 *
 * @param doc    A scene or prefab document.
 * @param newest The version this build writes.
 * @param what   What the document is, for the log line.
 * @param source Where it came from, for the log line.
 * @return False, having logged, when the version is missing, malformed or newer.
 */
inline bool readsFormatVersion(const nlohmann::json& doc, int newest, const char* what, const char* source) {
    const auto found  = doc.find("version");
    const bool whole  = found != doc.end() && found->is_number_integer() && fitsIn<int>(*found);
    const int version = whole ? found->get<int>() : 0;
    if (version <= 0) {
        LOG_ERROR_C("IO", "%s '%s' has no valid 'version'; refusing to load", what, source);
        return false;
    }
    if (version > newest) {
        LOG_ERROR_C(
            "IO",
            "%s '%s' is version %d, newer than this build reads (%d); refusing to load",
            what,
            source,
            version,
            newest
        );
        return false;
    }
    return true;
}

/**
 * @brief Make every number under @p node what a file should say: finite, each float its shortest decimal.
 *
 * A non-finite one becomes 0, logged: nlohmann::json dumps inf and NaN as `null`, which the
 * loaders throw on, so the check is on write and reads stay strict. A float's value - a
 * double that is exactly one - becomes the shortest decimal that reads back as that float,
 * so 0.92f is written 0.92, not 0.9200000166893005, and an unchanged value writes the same.
 *
 * @param node The subtree to walk.
 * @param what What the document is, for the log line.
 * @param path Scratch for the field's address; holds @p node's.
 */
inline void tidyNumbers(nlohmann::json& node, const char* what, std::string& path) {
    if (node.is_object()) {
        const std::size_t parent = path.size();
        for (auto& field : node.items()) {
            path += '/';
            path += field.key();
            tidyNumbers(field.value(), what, path);
            path.resize(parent);
        }
        return;
    }
    if (node.is_array()) {
        const std::size_t parent = path.size();
        for (std::size_t i = 0; i < node.size(); ++i) {
            path += '/';
            path += std::to_string(i);
            tidyNumbers(node[i], what, path);
            path.resize(parent);
        }
        return;
    }
    if (!node.is_number_float()) return;

    const double value = node.get<double>();
    if (!std::isfinite(value)) {
        LOG_WARNING_C("IO", "%s: %s is not a finite number; written as 0", what, path.c_str());
        node = 0.0;
        return;
    }
    const auto single = static_cast<float>(value);
    if (static_cast<double>(single) != value) return;
    char shortest[32];
    const std::to_chars_result written = std::to_chars(shortest, shortest + sizeof(shortest) - 1, single);
    *written.ptr = '\0';
    // Read back as to_chars wrote it, whatever the host's locale.
    double tidy = 0.0;
    const std::from_chars_result read = std::from_chars(shortest, written.ptr, tidy);
    if (read.ec == std::errc{} && read.ptr == written.ptr) node = tidy;
}

/**
 * @brief Hold @p doc to the rule above, from its root.
 *
 * @param doc  The document to walk.
 * @param what What the document is, for the log line.
 */
inline void tidyNumbers(nlohmann::json& doc, const char* what) {
    std::string path;
    tidyNumbers(doc, what, path);
}

/**
 * @brief Write @p doc to @p path, creating parent directories as needed.
 *
 * Writes a sibling temp file and renames it over the target, so a failed write
 * leaves the previous file intact.
 *
 * @param path The file to write.
 * @param doc  The document; mutable because its numbers are tidied (tidyNumbers).
 * @param what What the document is, for the log line.
 * @return False, having logged, on any failure.
 */
inline bool writeJsonFile(const std::filesystem::path& path, nlohmann::json& doc, const char* what) {
    tidyNumbers(doc, what);

    WholeFileWrite file(path, std::ios::openmode{}, what);
    if (!file) return false;
    // Trailing newline, or every diff reports "no newline at end of file".
    file.stream() << doc.dump(2) << '\n';
    return file.commit();
}

} // namespace Vkm::Engine::detail
