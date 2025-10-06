#include <iostream>
#include <cstdio>

#include "utils/logger.h"
#include "utils/build_info.h"

int main() {
    std::string rootDir = APP_ROOT_DIR;
    std::string logFile = rootDir + "/logs/log.log";
    Logger::init(logFile);

    printBuildInfo();
    return 0;
}
