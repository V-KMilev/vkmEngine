#pragma once

#include <memory>

#include "frame_info.h"
#include "frame_tracker.h"
#include "call_tracker.h"

/**
 * @class StatisticTracker
 * @brief Singleton class for gathering and managing engine run-time statistics for profiling and debugging.
 *
 * The StatisticTracker provides a thread-safe interface to collect and query performance statistics
 * across rendering, entity, and event systems. Its counters are accessed via macros that allow 
 * the tracking code to be enabled/disabled at compile time. This system is intended for 
 * use in both development and profiling builds, and is largely disabled for release builds.
 */
class StatisticTracker {
    public:
        StatisticTracker(const StatisticTracker& other) = delete;
        StatisticTracker& operator=(const StatisticTracker& other) = delete;

        StatisticTracker(StatisticTracker && other) = delete;
        StatisticTracker& operator=(StatisticTracker && other) = delete;

    public:
        /**
         * @brief Retrieve the singleton instance.
         * @return Reference to the global StatisticTracker.
         */
        static StatisticTracker& get();

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

        /**
         * @brief Record a graphics draw call (e.g., draw command issued to the GPU).
         */
        void recordDrawCall();
        /**
         * @brief Record the start of a render pass.
         */
        void recordRenderPass();
        /**
         * @brief Record a texture bind operation.
         */
        void recordTextureBind();
        /**
         * @brief Record a shader switch event.
         */
        void recordShaderSwitch();

        /**
         * @brief Record an ECS entity update.
         */
        void recordEntityUpdate();
        /**
         * @brief Record when a new entity is created.
         */
        void recordEntityCreate();
        /**
         * @brief Record when an entity is destroyed.
         */
        void recordEntityDestroy();

        /**
         * @brief Record when an event is dispatched.
         */
        void recordEventDispatch();
        /**
         * @brief Record when a listener subscribes to an event.
         */
        void recordEventSubscribe();
        /**
         * @brief Record when a listener unsubscribes or is removed.
         */
        void recordEventUnsubscribe();

    private:
        StatisticTracker();
        ~StatisticTracker();

    private:
        FrameInfo m_frameInfo;

        FrameTracker m_frameTracker;
        CallTracker m_callTracker;
};

/**
 * @def ENABLE_STATISTICS_TRACKING
 * @brief Compile-time toggle that enables statistics gathering in development/debug builds.
 *
 * If not explicitly set, statistics tracking is enabled for non-release (non-NDEBUG) builds and
 * disabled otherwise (release builds).
 */
#ifndef ENABLE_STATISTICS_TRACKING
    #ifdef NDEBUG
        #define ENABLE_STATISTICS_TRACKING 0
    #else
        #define ENABLE_STATISTICS_TRACKING 1
    #endif
#endif

#if ENABLE_STATISTICS_TRACKING

#define STATS_FRAME_UPDATE() StatisticTracker::get().update()

// Render system
#define STATS_RECORD_DRAW_CALL()     StatisticTracker::get().recordDrawCall()
#define STATS_RECORD_RENDER_PASS()   StatisticTracker::get().recordRenderPass()
#define STATS_RECORD_TEXTURE_BIND()  StatisticTracker::get().recordTextureBind()
#define STATS_RECORD_SHADER_SWITCH() StatisticTracker::get().recordShaderSwitch()

// Entity system
#define STATS_RECORD_ENTITY_UPDATE()  StatisticTracker::get().recordEntityUpdate()
#define STATS_RECORD_ENTITY_CREATE()  StatisticTracker::get().recordEntityCreate()
#define STATS_RECORD_ENTITY_DESTROY() StatisticTracker::get().recordEntityDestroy()

// Event system
#define STATS_RECORD_EVENT_DISPATCH()    StatisticTracker::get().recordEventDispatch()
#define STATS_RECORD_EVENT_SUBSCRIBE()   StatisticTracker::get().recordEventSubscribe()
#define STATS_RECORD_EVENT_UNSUBSCRIBE() StatisticTracker::get().recordEventUnsubscribe()

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
