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
        // Detect `#include "..."`. Leading whitespace allowed; the directive
        // must not be inside a line-comment.
        const size_t commentPos = line.find("//");
        size_t includePos = line.find("#include");
        if (includePos != std::string::npos && (commentPos == std::string::npos || commentPos > includePos)) {
            const size_t q1 = line.find('"', includePos);
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

} // namespace

std::string preprocessShaderFile(const std::string& filePath) {
    std::unordered_set<std::string> visited;
    return processFile(fs::path(filePath), visited);
}

} // namespace Engine
