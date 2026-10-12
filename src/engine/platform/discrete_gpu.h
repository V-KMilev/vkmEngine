#pragma once

/**
 * @brief Asks a two-GPU laptop's NVIDIA or AMD driver for the discrete GPU, which each reads off
 *        an export of the executable: written once, at file scope, in the main.cpp of each host
 *        that draws.
 *
 * Exports of a library are not read, so the executable carries them itself. On Linux the choice
 * is the environment's (getting-started.md, "On a laptop with two GPUs"), and this is nothing.
 */
#if defined(_WIN32)
#define VKM_PREFER_DISCRETE_GPU()                                         \
    extern "C" {                                                          \
    __declspec(dllexport) unsigned long NvOptimusEnablement = 1;          \
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;   \
    }
#else
#define VKM_PREFER_DISCRETE_GPU()
#endif
