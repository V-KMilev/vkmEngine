#pragma once

#include <string>
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
 * @param filePath Path to the top-level shader file (e.g. fragmentShader.shader).
 * @param defines  Optional list of `#define X` tokens to inject.
 * @return Fully expanded source string. Empty on read failure (logged).
 */
std::string preprocessShaderFile(const std::string& filePath,
                                 const std::vector<std::string>& defines = {});

} // namespace Engine
