#pragma once

// Single include point for GLFW: pulls in windows.h first on Win32 (undefining
// its ERROR/WARNING macros that clash with the logger) and disables GLFW's own
// GL header so the GL loader (GLEW) owns the function declarations.

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #undef ERROR
    #undef WARNING
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
