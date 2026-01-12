#pragma once

/**
 * @file gl_common.h
 * @brief Convenience header for OpenGL backend components.
 * 
 * This header provides a single include point for all commonly used
 * OpenGL backend headers. Include this in your render passes or systems
 * that need to interact with the OpenGL backend.
 * 
 * Usage:
 *   #include "gl_common.h"
 * 
 * Instead of:
 *   #include "gl_backend.h"
 *   #include "gl_view.h"
 *   #include "gl_mesh.h"
 *   // ... etc
 */

// Core backend components
#include "gl_backend.h"
#include "gl_view.h"

// Configuration
#include "gl_config.h"
#include "gl_texture_mapping.h"

// Resource wrappers
#include "gl_mesh.h"
#include "gl_material.h"
#include "gl_texture.h"
#include "gl_lights.h"

// OpenGL core wrappers (optional, for advanced usage)
// Uncomment if you need direct access to low-level OpenGL wrappers:
// #include "gl_context.h"
// #include "gl_shader.h"

/**
 * @namespace Engine
 * @brief Main engine namespace containing all rendering components.
 */

/**
 * @namespace Engine::GLConfig
 * @brief OpenGL backend configuration constants.
 * 
 * Contains centralized constants for:
 * - UBO binding points
 * - Texture unit slots
 * - Uniform names
 * - Resource limits
 */
