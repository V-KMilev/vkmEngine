#include <iostream>
#include <cstdio>

#include "logger.h"
#include "build_info.h"
#include "print_helper.h"

#include <vector>
#include "entity.h"
#include "component.h"

#include "event_manager.h"
#include "window_manager.h"

#include "glfw_include.h"

int main() {
    std::string rootDir = APP_ROOT_DIR;
    std::string logFile = rootDir + "/logs/log.log";
    Logger::init(logFile, "ENGINE", LogLevel::DEBUG);

    printBuildInfo();

    auto& windowManager = WindowManager::get();
    auto& eventManager = EventManager::get();

    windowManager.createWindow("VKM Engine");
    windowManager.updateMode(WindowMode::WINDOWED);

    while (!windowManager.shouldClose()) {

        eventManager.execute();

        if (!windowManager.updateInput()) break;
        if (!windowManager.swapBuffers()) break;
    }

    return 0;
}
