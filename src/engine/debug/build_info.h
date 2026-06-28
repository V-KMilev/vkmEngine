#pragma once

#include "logger.h"

namespace Engine {

/**
 * @brief Log the build banner (app name, version, branch, commit, date).
 *
 * Reads the APP_* macros injected by CMake as compile definitions; call once
 * at startup.
 */
inline void printBuildInfo() {
    LOG_INFO_C("BUILD", "------- Build Information -------");
    LOG_INFO_C("BUILD", "Running '%s' Version %s", APP_NAME, APP_VERSION);
    LOG_INFO_C("BUILD", "Major version: %s", APP_VERSION_MAJOR);
    LOG_INFO_C("BUILD", "Minor version: %s", APP_VERSION_MINOR);
    LOG_INFO_C("BUILD", "Patch version: %s", APP_VERSION_PATCH);
    LOG_INFO_C("BUILD", "Branch: %s", APP_BRANCH);
    LOG_INFO_C("BUILD", "Commit Hash: %s", APP_COMMIT_HASH);
    LOG_INFO_C("BUILD", "Build Date: %s", APP_BUILD_DATE);
    LOG_INFO_C("BUILD", "---------------------------------");
}

} // namespace Engine
