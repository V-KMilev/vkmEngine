#pragma once

/**
 * @brief The one way this project reaches a Windows system header.
 *
 * NOGDI is required: GDI defines ERROR as 0, breaking vkmLog's LogLevel::ERROR. The defines are
 * guarded because MinGW libstdc++ already defines NOMINMAX. windef.h's empty `far`, `near` and
 * `pascal` macros (and ERROR, WARNING) are undefined after. winsock2.h precedes windows.h, which
 * would otherwise pull in Winsock 1; mmsystem.h precedes the undefs, as it spells FAR and NEAR.
 * Safe to include unconditionally: everything is inside a _WIN32 guard.
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
    #include <mmsystem.h>

    #undef far
    #undef near
    #undef pascal
    #undef ERROR
    #undef WARNING
#endif
