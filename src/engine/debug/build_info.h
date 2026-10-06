#pragma once

#include "logger.h"

namespace Vkm::Engine {

/**
 * @brief Log the build banner: this binary, and each vkm module's hash.
 *
 * Reads the APP_* and VKM* macros CMake defines; call once at startup. The GL
 * strings are logged by the backend, as no context exists yet.
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
