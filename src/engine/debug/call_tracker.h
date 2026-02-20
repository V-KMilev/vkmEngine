#pragma once

#include <atomic>
#include <cstdint>

#include "debug/frame_info.h"

namespace Engine {

/**
 * @class CallTracker
 * @brief Tracks high-level system calls and updates within the engine, such as rendering, entity and event operations.
 *
 * This class maintains atomic counters for key actions in rendering, entity, and event systems
 * for profiling and debugging purposes. All counters are resettable and safely accessible from multiple threads.
 */
class CallTracker {
    public:
        CallTracker() = default;
        ~CallTracker() = default;

        CallTracker(const CallTracker& other) = delete;
        CallTracker& operator=(const CallTracker& other) = delete;

        CallTracker(CallTracker && other) = delete;
        CallTracker& operator=(CallTracker && other) = delete;

    public:
        /** @brief Increment draw call count (e.g., each graphics draw issued). */
        void recordDrawCall();
        /** @brief Increment render pass count (each render pass). */
        void recordRenderPass();
        /** @brief Increment texture bind count (each time a texture is bound). */
        void recordTextureBind();
        /** @brief Increment shader switch count (each shader program switch). */
        void recordShaderSwitch();

        // Entity system recording

        /** @brief Increment entity update count (each ECS update). */
        void recordEntityUpdate();
        /** @brief Increment entity create count (entity created). */
        void recordEntityCreate();
        /** @brief Increment entity destroy count (entity destroyed). */
        void recordEntityDestroy();

        // Event system recording

        /** @brief Increment event dispatch count (event fired/dispatched). */
        void recordEventDispatch();
        /** @brief Increment event subscribe count (listener subscribed). */
        void recordEventSubscribe();
        /** @brief Increment event unsubscribe count (listener removed). */
        void recordEventUnsubscribe();

        /** @brief Reset all statistics counters to zero. */
        void reset();

        /**
         * @brief Get render-related statistics.
         * @return RenderSystemInfo Structure containing render call statistics.
         */
        RenderSystemInfo getRenderInfo() const;

        /**
         * @brief Get entity system statistics.
         * @return EntitySystemInfo Structure containing entity call statistics.
         */
        EntitySystemInfo getEntityInfo() const;

        /**
         * @brief Get event system statistics.
         * @return EventSystemInfo Structure containing event call statistics.
         */
        EventSystemInfo getEventInfo() const;

    private:
        struct {
            std::atomic<uint32_t> drawCalls{0};
            std::atomic<uint32_t> renderPasses{0};
            std::atomic<uint32_t> textureBinds{0};
            std::atomic<uint32_t> shaderSwitches{0};
        } m_render;

        struct {
            std::atomic<uint32_t> entityUpdates{0};
            std::atomic<uint32_t> entityCreates{0};
            std::atomic<uint32_t> entityDestroys{0};
        } m_entity;

        struct {
            std::atomic<uint32_t> eventsDispatched{0};
            std::atomic<uint32_t> eventsSubscribed{0};
            std::atomic<uint32_t> eventsUnsubscribed{0};
        } m_event;
};

} // namespace Engine
