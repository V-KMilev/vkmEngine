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
#include "statistics.h"
// #include "input_handle.h"

#include "glfw_include.h"

int main() {
    std::string rootDir = APP_ROOT_DIR;
    std::string logFile = rootDir + "/logs/log.log";
    Logger::init(logFile, "ENGINE", LogLevel::DEBUG);

    printBuildInfo();

    auto& windowManager = WindowManager::get();
    auto& eventManager = EventManager::get();
    auto& statisticTracker = StatisticTracker::get();

    windowManager.createWindow("VKM Engine");
    windowManager.updateMode(WindowMode::WINDOWED);

    // windowManager.setVSync(true);
    windowManager.setFramerate(300);

    try {
        while (windowManager.beginFrame()) {
            // Update the input before everything else
            // Gives the lowest input latency
            if (!windowManager.updateInput()) break;

            eventManager.executeAsync();

            // Swap the buffers and present the frame (applies frame limiting)
            if (!windowManager.swapBuffers()) break;

            {
                statisticTracker.update();

                // Console output with statistics
                const auto& info = statisticTracker.getFrameInfo();
                printf("\r[%lu] FPS: %.2f (%.4fms) | Draws: %u | Entities: %u",
                    info.frameIndex,
                    info.frameRateInfo.frameRate,
                    info.frameRateInfo.frameTime,
                    info.renderSystemInfo.drawCalls,
                    info.entitySystemInfo.entityUpdates
                );
                fflush(stdout);
            }
        }
    } catch (const std::exception& e) {
        LOG_FATAL("Exception: %s", e.what());
    } catch (...) {
        LOG_FATAL("Unknown exception");
    }

    return 0;
}
