#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace Engine {

/**
 * @brief Load a shader file and resolve `#include "path"` directives.
 *
 * GLSL itself has no portable #include directive. This preprocessor scans
 * the loaded text line-by-line and inlines referenced files relative to
 * the including file's directory. Cycles and re-inclusion are handled by
 * an include-guard set kept across the recursion.
 *
 * Supported syntax (one per line, leading whitespace tolerated):
 *     #include "relative/or/absolute/path.glsl"
 *
 * The included file is spliced in place; the original line is preserved
 * as a `// #include "..."` comment so compile errors remain navigable.
 *
 * If @p defines is non-empty, each entry becomes a `#define <name>` line
 * injected right after the file's `#version` directive (GLSL requires
 * #version to be the first non-comment line, so defines can't go above
 * it). Used by the shader variant cache to gate optional PBR features by
 * `#ifdef HAS_TRANSMISSION` / etc. Each entry should be a plain token or
 * `TOKEN value` - the preprocessor doesn't validate.
 *
 * @param filePath Path to the top-level shader file (e.g. fragment.shader).
 * @param defines  Optional list of `#define X` tokens to inject.
 * @return Fully expanded source string. Empty on read failure (logged).
 */
std::string preprocessShaderFile(const std::string& filePath,
                                 const std::vector<std::string>& defines = {});

/**
 * @brief Same as preprocessShaderFile, but also reports the parent
 *        directories of every `#include`d file traversed.
 *
 * Used by the hot-reload plumbing (`watchShader`) to extend the
 * FileWatcher across every directory that contributes to the compiled
 * source: an edit to a shared `_helpers/lighting.glsl` should bump
 * every consumer's asset version, not just the helper's own dir.
 *
 * @p outIncludedDirs is populated with canonical directory strings
 * for every file pulled in via `#include`. The top-level file's own
 * directory is NOT included (callers already watch it).
 */
std::string preprocessShaderFile(const std::string& filePath,
                                 const std::vector<std::string>& defines,
                                 std::unordered_set<std::string>& outIncludedDirs);

} // namespace Engine
