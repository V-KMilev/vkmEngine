#pragma once

#include <string>

namespace Vkm::Engine {

/**
 * @brief The engine constants every shader stage is compiled with.
 *
 * GLSL cannot see a C++ header, so this writes the constants themselves as GLSL
 * (`Config::MAX_LIGHTS`, the probe capacity, IBL mip counts, `MODE_*` ordinals,
 * gl_bindings.h's points and units), and ConstantsInstalled hands the text to the shader
 * loader as a prelude. The GPU test suite calls it too, so it compiles the shipped shaders
 * exactly as the backend does, without a window.
 *
 * @return GLSL declarations, ready to sit under the `#version` line.
 */
std::string shaderConstants();

} // namespace Vkm::Engine
