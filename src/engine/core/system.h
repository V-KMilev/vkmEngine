#pragma once

#include <cstdint>

#include "visibility/visibility.h"

namespace Engine {

class Scene;
class ResourceManager;

/**
 * @brief Per-frame state bundle passed to all systems.
 *
 * Provides a uniform interface for systems to access shared per-frame data.
 * visibility is populated by the visibility system and consumed by later systems.
 */
struct FrameContext {
    Scene& scene;
    ResourceManager& resources;
    float deltaTime;
    uint32_t viewportWidth;
    uint32_t viewportHeight;
    Visibility visibility;
};

/**
 * @brief Abstract base class for per-frame systems.
 *
 * Systems are executed sequentially in registration order. Each system
 * reads from and/or writes to the FrameContext.
 */
class System {
    public:
        virtual ~System() = default;

        System(const System& other) = delete;
        System& operator=(const System& other) = delete;

        System(System && other) = delete;
        System& operator=(System && other) = delete;

        /**
         * @brief Execute this system for the current frame.
         * @param ctx The shared FrameContext for this frame.
         */
        virtual void update(FrameContext& ctx) = 0;

    protected:
        System() = default;
};

} // namespace Engine
