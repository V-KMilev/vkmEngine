#include <iostream>
#include <cstdio>
#include <vector>
#include <thread>


#include "logger.h"
#include "build_info.h"
#include "print_helper.h"

#include "entity.h"
#include "component.h"

#include "event_manager.h"
#include "window_manager.h"

#include "glfw_include.h"

#include "input_handle.h"

class Test {
    public:
        Test() = default;
        ~Test() = default;

        Test(const Test& other) = delete;
        Test& operator=(const Test& other) = delete;

        Test(Test&& other) = delete;
        Test& operator=(Test&& other) =delete;

    private:
};

class EntityTest : public Test {
    public:
        EntityTest() {
            m_entities.reserve(300);
        }
        ~EntityTest() = default;

        EntityTest(const EntityTest& other) = delete;
        EntityTest& operator=(const EntityTest& other) = delete;

        EntityTest(EntityTest&& other) = delete;
        EntityTest& operator=(EntityTest&& other) =delete;

    public:
        void addEnity(uint32_t idx) {
            m_entities.emplace_back(
                idx,
                EntityType::NONE,
                std::make_shared<Component>(idx, ComponentType::NONE)
            );

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

    private:
        std::vector<Entity> m_entities;
};


int main() {
    std::string rootDir = APP_ROOT_DIR;
    std::string logFile = rootDir + "/logs/log.log";
    Logger::init(logFile, "ENGINE", LogLevel::DEBUG);

    printBuildInfo();

    auto& windowManager = WindowManager::get();
    auto& eventManager = EventManager::get();

    windowManager.createWindow("VKM Engine");
    windowManager.updateMode(WindowMode::WINDOWED);

    EntityTest entityTest;

    bool lockQ = false;
    bool lockW = false;
    bool lockE = false;

    while (!windowManager.shouldClose()) {
        if (!lockQ && windowManager.getInputHandle()->isPressed(GLFW_KEY_Q)) {
            lockQ = true;
            eventManager.push(Event(EventPriority::LOW, [&entityTest]() {
                entityTest.addEnity(0);
            }, "addEntity #0"));
        }
        if (!lockW && windowManager.getInputHandle()->isPressed(GLFW_KEY_W)) {
            lockW = true;
            eventManager.push(Event(EventPriority::MEDIUM, [&entityTest]() {
                entityTest.addEnity(1);
            }, "addEntity #1"));
        }
        if (!lockE && windowManager.getInputHandle()->isPressed(GLFW_KEY_E)) {
            lockE = true;
            eventManager.push(Event(EventPriority::HIGH, [&entityTest]() {
                entityTest.addEnity(2);
            }, "addEntity #2"));
        }

        if (lockQ && windowManager.getInputHandle()->isReleased(GLFW_KEY_Q)) {
            lockQ = false;
        }
        if (lockW && windowManager.getInputHandle()->isReleased(GLFW_KEY_W)) {
            lockW = false;
        }
        if (lockE && windowManager.getInputHandle()->isReleased(GLFW_KEY_E)) {
            lockE = false;
        }

        eventManager.execute();

        if (!windowManager.updateInput()) break;
        if (!windowManager.swapBuffers()) break;
    }

    return 0;
}
