#pragma once

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

#include "logger.h"

namespace Vkm::Engine::detail {

// Open and parse a JSON file. On failure logs an error tagged with @p what and
// returns false, leaving @p out untouched.
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

// Replace every non-finite number under @p node with 0, naming each one in the
// log: @p what tags the document and @p path is the scratch buffer the field's
// address is built in, grown and trimmed as the walk descends rather than
// rebuilt per node.
//
// JSON cannot write an infinity or a NaN, so nlohmann::json dumps both as
// `null` - and a null where a float was is a type error the component loaders
// throw on, which aborts the whole load. One unrepresentable field would cost
// every entity in the file, so the document is checked where it is built rather
// than where it is read: the engine does not write a document it cannot read
// back. Zero is what a field with no value gets; the field was already wrong -
// an infinite volume is silent and an infinite position is nowhere - and the
// log says which one, so the author can set it to what they meant.
//
// The read side is deliberately left strict. Teaching the loaders that `null`
// means "keep the default" would make it a legal token in every scalar field of
// the scene format, permanently, where this changes nothing about what a
// well-formed file looks like - a finite number still writes exactly as it did.
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
    // Integers cannot be non-finite, so only the float nodes are asked.
    if (!node.is_number_float() || std::isfinite(node.get<double>())) return;

    LOG_WARNING_C("IO", "%s: %s is not a finite number; written as 0", what, path.c_str());
    node = 0.0;
}

// Hold @p doc to the rule above, from its root.
inline void writeNonFiniteAsZero(nlohmann::json& doc, const char* what) {
    std::string path;
    writeNonFiniteAsZero(doc, what, path);
}

// Write @p doc to @p path, creating parent directories as needed. The dump goes
// to a sibling temp file that is renamed over the target only once the stream
// says it wrote cleanly, so a full disk or a crash mid-write leaves the previous
// file intact instead of a truncated one. Returns false (logging, tagged with
// @p what) on any failure; the caller must not report a save it did not get.
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
        // A trailing newline, because these are text files: without one a diff
        // reports "no newline at end of file" on every write and a terminal
        // runs the next prompt into the last brace.
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
        LOG_ERROR_C("IO", "%s: rename '%s' -> '%s' failed: %s", what,
            tmp.string().c_str(), path.string().c_str(), ec.message().c_str());
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace Vkm::Engine::detail
