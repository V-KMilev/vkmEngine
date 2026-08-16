#pragma once

/**
 * @brief OpenGL GPU profiler facade over Tracy.
 *
 * Tracy's TracyOpenGL.hpp inlines glGenQueries / glQueryCounter / GL_TIMESTAMP
 * into the caller's TU but doesn't pull in a loader itself, so this header
 * pulls GLEW first. Including from a non-OpenGL target therefore fails fast
 * (no <GL/glew.h>) instead of silently breaking via include ordering.
 *
 * Pulls in CPU macros from profiler.h so a single include covers both.
 *
 * Lifecycle:
 *   PROFILE_GPU_CONTEXT();   // once, on the GL thread, after the GL context exists
 *   PROFILE_GPU_COLLECT();   // once per frame, after GPU work has been submitted
 *   PROFILE_GPU_SCOPE("Shadow");                // string literal
 *   PROFILE_GPU_SCOPE_NAMED(passName.c_str());  // dynamic name
 */

#include "debug/profiler.h"

#if VKM_PROFILER

#include <GL/glew.h>
#include <tracy/TracyOpenGL.hpp>

#define PROFILE_GPU_CONTEXT()               TracyGpuContext
#define PROFILE_GPU_COLLECT()               TracyGpuCollect
#define PROFILE_GPU_SCOPE(name_literal) \
    TracyGpuNamedZone(VKM_PROFILE_CONCAT(___profile_gpu_zone_, __LINE__), name_literal, true)
#define PROFILE_GPU_SCOPE_NAMED(name_cstr) \
    TracyGpuZoneTransient(VKM_PROFILE_CONCAT(___profile_gpu_zone_, __LINE__), name_cstr, true)

#else

#define PROFILE_GPU_CONTEXT()               ((void)0)
#define PROFILE_GPU_COLLECT()               ((void)0)
#define PROFILE_GPU_SCOPE(name_literal)     ((void)0)
#define PROFILE_GPU_SCOPE_NAMED(name_cstr)  ((void)0)

#endif
