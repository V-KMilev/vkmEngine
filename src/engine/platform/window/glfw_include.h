#pragma once

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #undef ERROR
    #undef WARNING
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>