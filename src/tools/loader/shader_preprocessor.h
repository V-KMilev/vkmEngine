#pragma once

#include <string>

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
 * @param filePath Path to the top-level shader file (e.g. fragmentShader.shader).
 * @return Fully expanded source string. Empty on read failure (logged).
 */
std::string preprocessShaderFile(const std::string& filePath);

} // namespace Engine
