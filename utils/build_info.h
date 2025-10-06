#pragma once

#include "logger.h"

void printBuildInfo() {
    LOG_INFO("------- Build Information -------");
    LOG_INFO("Running '%s' Version %s", APP_NAME, APP_VERSION);
    LOG_INFO("Major version: %s", APP_VERSION_MAJOR);
    LOG_INFO("Minor version: %s", APP_VERSION_MINOR);
    LOG_INFO("Patch version: %s", APP_VERSION_PATCH);
    LOG_INFO("Branch: %s", APP_BRANCH);
    LOG_INFO("Commit Hash: %s", APP_COMMIT_HASH);
    LOG_INFO("Build Date: %s", APP_BUILD_DATE);
    LOG_INFO("---------------------------------");
}
