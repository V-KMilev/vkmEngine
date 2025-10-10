#include <iostream>
#include <cstdio>

#include "modules/vkmLogger/logger.h"
#include "utils/build_info.h"

int main() {
    std::string rootDir = APP_ROOT_DIR;
    std::string logFile = rootDir + "/logs/log.log";
    Logger::init(logFile, "ENGINE");

    printBuildInfo();
    return 0;
}
