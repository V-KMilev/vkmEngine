#pragma once

#include <cstdint>
#include <vector>

#include "core/memory/types.h"
#include "system/visibility/visibility.h"

namespace Engine {

class Scene;
class ResourceManager;
class WindowManager;
class StatisticTracker;

/**
 * @brief Per-frame state bundle passed to all systems.
 *
 * Provides a uniform interface for systems to access shared per-frame data.
 * visibility is a non-owning pointer to persistent storage (owned by VisibilitySystem)
 * to avoid per-frame vector allocation/deallocation.
 */
struct FrameContext {
    Scene& scene;
    ResourceManager& resources;
    WindowManager& window;
    StatisticTracker& statistics;
    float deltaTime;
    uint32_t viewportWidth;
    uint32_t viewportHeight;
    const Visibility* visibility = nullptr;
};

/**
 * @brief Declares which component types a system reads and writes.
 *
 * Used for dependency validation and as a foundation for future parallel scheduling.
 * Systems with no write-write conflicts on the same TypeIds could potentially run concurrently.
 */
struct SystemAccess {
    std::vector<TypeId> reads;   ///< Component TypeIds this system reads from
    std::vector<TypeId> writes;  ///< Component TypeIds this system writes to
};

/**
 * @brief Abstract base class for per-frame systems.
 *
 * Systems are executed sequentially in registration order. Each system
 * reads from and/or writes to the FrameContext. Systems support lifecycle
 * hooks (init/shutdown) and can be enabled/disabled at runtime.
 */
class System {
    public:
        virtual ~System() = default;

        System(const System& other) = delete;
        System& operator=(const System& other) = delete;

        System(System && other) = delete;
        System& operator=(System && other) = delete;

    public:
        /**
         * @brief Called once after all systems are registered, before the first update.
         * @param ctx The shared FrameContext for initialization.
         */
        virtual void init(FrameContext& ctx) {}

        /**
         * @brief Execute this system for the current frame.
         * @param ctx The shared FrameContext for this frame.
         */
        virtual void update(FrameContext& ctx) = 0;

        /**
         * @brief Called once on engine shutdown, in reverse registration order.
         */
        virtual void shutdown() {}

        /**
         * @brief Declare which component types this system reads and writes.
         *
         * Override to enable dependency validation and future parallel scheduling.
         * Default returns empty (no declarations).
         */
        virtual SystemAccess declareAccess() const { return {}; }

        bool isEnabled() const { return m_enabled; }
        void setEnabled(bool enabled) { m_enabled = enabled; }

    protected:
        System() = default;

    private:
        bool m_enabled = true;
};

} // namespace Engine
