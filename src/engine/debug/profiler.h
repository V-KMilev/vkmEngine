#pragma once

/**
 * @brief CPU profiler facade over Tracy, independent of any render backend.
 *
 * Engine code never includes Tracy directly. Every macro is a free no-op when
 * VKM_PROFILER is unset. GPU zones live in gl_profiler.h, as Tracy's OpenGL
 * header inlines GL calls into the caller's TU.
 *
 *   PROFILE_FRAME_MARK();                 // once per frame, end of loop
 *   PROFILE_SCOPE("StageName");           // string literal
 *   PROFILE_SCOPE_NAMED(name.c_str());    // dynamic name
 *   PROFILE_PLOT("Draws", drawCount);     // numeric series
 */

#ifndef VKM_PROFILER
    #define VKM_PROFILER 0
#endif

#if VKM_PROFILER

// Tracy calls vsnprintf without including <cstdio>.
#include <cstdio>

#include <tracy/Tracy.hpp>

// __LINE__ keeps zone variable names unique, so several scopes in one block don't collide.
#define VKM_PROFILE_CONCAT_(a, b) a##b
#define VKM_PROFILE_CONCAT(a, b)  VKM_PROFILE_CONCAT_(a, b)

#define PROFILE_FRAME_MARK()              FrameMark
#define PROFILE_SCOPE(name_literal) \
    ZoneNamedN(VKM_PROFILE_CONCAT(vkmProfileZone_, __LINE__), name_literal, true)
#define PROFILE_SCOPE_NAMED(name_cstr) \
    ZoneTransientN(VKM_PROFILE_CONCAT(vkmProfileZone_, __LINE__), name_cstr, true)
#define PROFILE_PLOT(name_literal, value) TracyPlot(name_literal, value)

#else

// Arguments go into an unevaluated sizeof rather than dropped, so a value that exists
// only to be plotted raises no unused-variable warning, at no cost.
#define PROFILE_FRAME_MARK()                ((void)0)
#define PROFILE_SCOPE(name_literal)         ((void)sizeof(name_literal))
#define PROFILE_SCOPE_NAMED(name_cstr)      ((void)sizeof(name_cstr))
#define PROFILE_PLOT(name_literal, value)   ((void)sizeof(name_literal), (void)sizeof(value))

#endif
