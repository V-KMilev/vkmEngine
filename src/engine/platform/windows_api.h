#pragma once

/**
 * @brief The one way this project reaches a Windows system header.
 *
 * Three defines have to precede the first <windows.h> in a translation unit, and
 * NOGDI is not optional: the GDI half defines ERROR as 0, so vkmLog's
 * LogLevel::ERROR stops parsing. WIN32_LEAN_AND_MEAN and NOMINMAX are guarded
 * because the MinGW libstdc++ headers already define the second.
 *
 * windef.h then defines `far`, `near` and `pascal` - empty macros kept since the
 * 16-bit days - which turn an ordinary identifier of any of those names into a
 * syntax error somewhere else in the file. They are undefined again here, and so
 * are ERROR and WARNING: NOGDI should have kept both out, and undefining what is
 * not defined costs nothing, but a level named ERROR is how this breaks and it
 * breaks in a place that has nothing to do with Windows.
 *
 * winsock2.h comes first because windows.h would otherwise pull in Winsock 1,
 * and a translation unit cannot have both.
 *
 * Everything is inside a _WIN32 guard, so a caller includes it unconditionally
 * and it costs nothing elsewhere. Written once because getting any part of it
 * wrong fails on a platform most of this tree is not built on.
 */
#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef NOGDI
        #define NOGDI
    #endif

    #include <winsock2.h>
    #include <windows.h>

    #undef far
    #undef near
    #undef pascal
    #undef ERROR
    #undef WARNING
#endif
