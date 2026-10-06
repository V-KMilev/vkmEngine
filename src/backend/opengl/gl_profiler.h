#pragma once

/**
 * @brief OpenGL GPU profiler facade over Tracy.
 *
 * TracyOpenGL.hpp inlines GL timer queries into the caller's TU without a loader, so this pulls
 * GLEW first: a non-OpenGL target fails fast rather than breaking silently on include order.
 * Also pulls in the CPU macros from profiler.h.
 *
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
    TracyGpuNamedZone(VKM_PROFILE_CONCAT(vkmProfileGpuZone_, __LINE__), name_literal, true)
#define PROFILE_GPU_SCOPE_NAMED(name_cstr) \
    TracyGpuZoneTransient(VKM_PROFILE_CONCAT(vkmProfileGpuZone_, __LINE__), name_cstr, true)

#else

#define PROFILE_GPU_CONTEXT()               ((void)0)
#define PROFILE_GPU_COLLECT()               ((void)0)
#define PROFILE_GPU_SCOPE(name_literal)     ((void)0)
#define PROFILE_GPU_SCOPE_NAMED(name_cstr)  ((void)0)

#endif
