#include <iostream>
#include <cstdio>

#include "logger.h"
#include "build_info.h"
#include "print_helper.h"

#include <vector>
#include "entity.h"
#include "component.h"

int main() {
    std::string rootDir = APP_ROOT_DIR;
    std::string logFile = rootDir + "/logs/log.log";
    Logger::init(logFile, "ENGINE", LogLevel::DEBUG);

    printBuildInfo();

    std::vector<Entity> entitys;
    entitys.reserve(5);  // Pre-allocate to avoid reallocations

    for(int idx = 0; idx < 5; idx++) {
        entitys.emplace_back(
            idx,
            EntityType::NONE,
            std::make_shared<Component>(idx, ComponentType::NONE)
        );
    }

    return 0;
}
