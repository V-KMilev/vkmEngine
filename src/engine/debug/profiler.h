#pragma once

/**
 * @brief CPU profiler facade over Tracy. Backend-agnostic.
 *
 * Engine code never includes Tracy headers directly - go through these macros.
 * Every macro expands to a zero-cost no-op when VKM_PROFILER is unset, so
 * release builds pay nothing. Switching profiler (Optick, minitrace, custom)
 * only changes this header.
 *
 * GPU zones live in the GL backend (gl_profiler.h) because Tracy's OpenGL header inlines
 * GL calls into the caller's TU. Including this header is cheap and pulls in
 * no GL state - prefer it everywhere unless you need a GPU zone.
 *
 * Usage:
 *   PROFILE_FRAME_MARK();                 // once per frame, end of loop
 *   PROFILE_SCOPE("StageName");           // string literal
 *   PROFILE_SCOPE_NAMED(name.c_str());    // dynamic name
 *   PROFILE_PLOT("Draws", drawCount);     // numeric series
 */

#ifndef VKM_PROFILER
    #define VKM_PROFILER 0
#endif

#if VKM_PROFILER

#include <tracy/Tracy.hpp>

// __LINE__ keeps every emitted zone variable name unique per call site so
// multiple PROFILE_SCOPE / PROFILE_SCOPE_NAMED calls in the same brace
// block don't collide on Tracy's stack-local zone variable.
#define VKM_PROFILE_CONCAT_(a, b) a##b
#define VKM_PROFILE_CONCAT(a, b)  VKM_PROFILE_CONCAT_(a, b)

#define PROFILE_FRAME_MARK()              FrameMark
#define PROFILE_SCOPE(name_literal) \
    ZoneNamedN(VKM_PROFILE_CONCAT(___profile_zone_, __LINE__), name_literal, true)
#define PROFILE_SCOPE_NAMED(name_cstr) \
    ZoneTransientN(VKM_PROFILE_CONCAT(___profile_zone_, __LINE__), name_cstr, true)
#define PROFILE_PLOT(name_literal, value) TracyPlot(name_literal, value)

#else

#define PROFILE_FRAME_MARK()                ((void)0)
#define PROFILE_SCOPE(name_literal)         ((void)0)
#define PROFILE_SCOPE_NAMED(name_cstr)      ((void)0)
#define PROFILE_PLOT(name_literal, value)   ((void)0)

#endif
