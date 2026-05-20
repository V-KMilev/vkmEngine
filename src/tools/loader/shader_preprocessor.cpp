#include "loader/shader_preprocessor.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "logger.h"

namespace Engine {

namespace {

namespace fs = std::filesystem;

/// Recursive worker. visited holds the canonical paths already included so
/// a cyclic include (#include "a.glsl" -> "b.glsl" -> "a.glsl") just drops
/// to a comment instead of looping forever.
std::string processFile(const fs::path& filePath,
                        std::unordered_set<std::string>& visited) {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(filePath, ec);
    const std::string key = ec ? filePath.string() : canonical.string();

    if (!visited.insert(key).second) {
        // Already included once - emit a marker comment so the consuming
        // shader stays valid GLSL and the dev can see why nothing landed
        // a second time.
        return "// (skipped duplicate include: " + filePath.string() + ")\n";
    }

    std::ifstream in(filePath);
    if (!in) {
        LOG_ERROR("ShaderPreprocessor: cannot open '%s'", filePath.string().c_str());
        return "// (failed to open: " + filePath.string() + ")\n";
    }

    const fs::path basedir = filePath.parent_path();
    std::ostringstream out;
    std::string line;

    while (std::getline(in, line)) {
        // Only treat `#include` as a directive when it's the FIRST non-
        // whitespace token on the line. That keeps the preprocessor from
        // false-matching `#include` inside comments / docstrings / strings.
        size_t firstNonWS = 0;
        while (firstNonWS < line.size() &&
               (line[firstNonWS] == ' ' || line[firstNonWS] == '\t')) {
            ++firstNonWS;
        }
        const bool isDirective = line.compare(firstNonWS, 8, "#include") == 0;
        if (isDirective) {
            const size_t q1 = line.find('"', firstNonWS + 8);
            const size_t q2 = q1 != std::string::npos ? line.find('"', q1 + 1) : std::string::npos;
            if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1) {
                const std::string rel = line.substr(q1 + 1, q2 - q1 - 1);
                const fs::path target = basedir / rel;

                // Preserve the original directive as a comment so compile
                // errors in the inlined body retain context for the reader.
                out << "// " << line << "\n";
                out << processFile(target, visited);
                continue;
            }
            LOG_WARNING("ShaderPreprocessor: malformed #include in '%s': %s",
                filePath.string().c_str(), line.c_str());
        }
        out << line << "\n";
    }
    return out.str();
}

/// Splice a `#define` block right after the file's `#version` line.
/// GLSL requires #version to be the first non-comment, non-empty line; any
/// #define above it is a compile error, so we have to find the version
/// directive ourselves and inject below it. If no #version line is found
/// (rare: e.g. an included snippet rendered standalone) we prepend at the
/// top - the GLSL compiler will surface its own error in that case.
std::string injectDefines(const std::string& src,
                          const std::vector<std::string>& defines) {
    if (defines.empty()) return src;

    std::ostringstream block;
    block << "// Variant defines injected by the shader preprocessor:\n";
    for (const auto& d : defines) block << "#define " << d << "\n";

    const size_t versionPos = src.find("#version");
    if (versionPos == std::string::npos) {
        return block.str() + src;
    }
    const size_t eol = src.find('\n', versionPos);
    if (eol == std::string::npos) {
        return src + "\n" + block.str();
    }
    return src.substr(0, eol + 1) + block.str() + src.substr(eol + 1);
}

} // namespace

std::string preprocessShaderFile(const std::string& filePath,
                                 const std::vector<std::string>& defines) {
    std::unordered_set<std::string> visited;
    std::string src = processFile(fs::path(filePath), visited);
    return injectDefines(src, defines);
}

} // namespace Engine
