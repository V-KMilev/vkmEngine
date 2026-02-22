#pragma once

#include <memory>

#include "debug/frame_info.h"
#include "debug/frame_tracker.h"
#include "debug/call_tracker.h"

namespace Engine {

/**
 * @class StatisticTracker
 * @brief Gathers and manages engine run-time statistics for profiling and debugging.
 *
 * Provides a thread-safe interface to collect and query performance statistics
 * across rendering, entity, and event systems. Its counters are accessed via macros that allow
 * the tracking code to be enabled/disabled at compile time. This system is intended for
 * use in both development and profiling builds, and is largely disabled for release builds.
 *
 * Owned by Engine; accessed globally via Engine::getStatistics() free function.
 */
class StatisticTracker {
    public:
        StatisticTracker();
        ~StatisticTracker();

        StatisticTracker(const StatisticTracker& other) = delete;
        StatisticTracker& operator=(const StatisticTracker& other) = delete;

        StatisticTracker(StatisticTracker && other) = delete;
        StatisticTracker& operator=(StatisticTracker && other) = delete;

    public:
        /**
         * @brief Update statistics for the current frame.
         */
        void update();

        /**
         * @brief Reset all collected statistics (frame and call counters).
         */
        void reset();

        /**
         * @brief Get the complete snapshot of frame information.
         * @return Current FrameInfo structure.
         */
        const FrameInfo& getFrameInfo() const { return m_frameInfo; }

    public:
        // Recording methods (called by macros, thread-safe)

        void recordDrawCall();
        void recordRenderPass();
        void recordTextureBind();
        void recordShaderSwitch();

        void recordEntityUpdate();
        void recordEntityCreate();
        void recordEntityDestroy();

        void recordEventDispatch();
        void recordEventSubscribe();
        void recordEventUnsubscribe();

    private:
        FrameInfo m_frameInfo;

        FrameTracker m_frameTracker;
        CallTracker m_callTracker;
};

/**
 * @brief Global accessor for the Engine-owned StatisticTracker.
 *
 * Defined in engine.cpp. Used by STATS_* macros to avoid including engine.h.
 */
StatisticTracker& getStatistics();

} // namespace Engine

/**
 * @def ENABLE_STATISTICS_TRACKING
 * @brief Compile-time toggle that enables statistics gathering in development/debug builds.
 */
#ifndef ENABLE_STATISTICS_TRACKING
    #ifdef NDEBUG
        #define ENABLE_STATISTICS_TRACKING 0
    #else
        #define ENABLE_STATISTICS_TRACKING 1
    #endif
#endif

#if ENABLE_STATISTICS_TRACKING

#define STATS_FRAME_UPDATE() Engine::getStatistics().update()

// Render system
#define STATS_RECORD_DRAW_CALL()     Engine::getStatistics().recordDrawCall()
#define STATS_RECORD_RENDER_PASS()   Engine::getStatistics().recordRenderPass()
#define STATS_RECORD_TEXTURE_BIND()  Engine::getStatistics().recordTextureBind()
#define STATS_RECORD_SHADER_SWITCH() Engine::getStatistics().recordShaderSwitch()

// Entity system
#define STATS_RECORD_ENTITY_UPDATE()  Engine::getStatistics().recordEntityUpdate()
#define STATS_RECORD_ENTITY_CREATE()  Engine::getStatistics().recordEntityCreate()
#define STATS_RECORD_ENTITY_DESTROY() Engine::getStatistics().recordEntityDestroy()

// Event system
#define STATS_RECORD_EVENT_DISPATCH()    Engine::getStatistics().recordEventDispatch()
#define STATS_RECORD_EVENT_SUBSCRIBE()   Engine::getStatistics().recordEventSubscribe()
#define STATS_RECORD_EVENT_UNSUBSCRIBE() Engine::getStatistics().recordEventUnsubscribe()

#else

// No-ops in release builds
#define STATS_FRAME_UPDATE()             ((void)0)

#define STATS_RECORD_DRAW_CALL()         ((void)0)
#define STATS_RECORD_RENDER_PASS()       ((void)0)
#define STATS_RECORD_TEXTURE_BIND()      ((void)0)
#define STATS_RECORD_SHADER_SWITCH()     ((void)0)

#define STATS_RECORD_ENTITY_UPDATE()     ((void)0)
#define STATS_RECORD_ENTITY_CREATE()     ((void)0)
#define STATS_RECORD_ENTITY_DESTROY()    ((void)0)

#define STATS_RECORD_EVENT_DISPATCH()    ((void)0)
#define STATS_RECORD_EVENT_SUBSCRIBE()   ((void)0)
#define STATS_RECORD_EVENT_UNSUBSCRIBE() ((void)0)

#endif
