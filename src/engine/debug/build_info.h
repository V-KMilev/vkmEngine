#pragma once

#include "logger.h"

namespace Vkm::Engine {

/**
 * @brief Log the build banner: what this binary is, and what it is made of.
 *
 * Mirrors the editor's About dialog - the same versions above, the same
 * per-module commits below, so a log and a screenshot describe the build the
 * same way. Each vkm module is its own repository, so one hash per module says
 * what one tree commit cannot.
 *
 * Reads the APP_* and VKM* macros injected by CMake as compile definitions;
 * call once at startup. The API and renderer strings are absent here because
 * the GL context does not exist yet - the backend logs those when it opens one.
 */
inline void printBuildInfo() {
    LOG_INFO_C("BUILD", "------- Build Information -------");
    LOG_INFO_C("BUILD", "Running '%s' v%s", APP_NAME, APP_VERSION);
    LOG_INFO_C("BUILD", "Build:  %s (%s)", APP_BUILD_DATE, APP_BRANCH);
    LOG_INFO_C("BUILD", "--------------- Debug -----------");
    LOG_INFO_C("BUILD", "vkmEngine: %s @ %.8s", APP_VERSION,     APP_COMMIT_HASH);
    LOG_INFO_C("BUILD", "vkmGL:     %s @ %.8s", VKM_GL_VERSION,  VKM_GL_COMMIT_HASH);
    LOG_INFO_C("BUILD", "vkmLog:    %s @ %.8s", VKM_LOG_VERSION, VKM_LOG_COMMIT_HASH);
    LOG_INFO_C("BUILD", "---------------------------------");
}

} // namespace Vkm::Engine
