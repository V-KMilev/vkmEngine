#pragma once

#include <cstdint>

// Frame timing information
struct FrameRateInfo {
    float frameTime    = 0.0f;
    float frameRate    = 0.0f;
    float minFrameTime = 0.0f;
    float maxFrameTime = 0.0f;
};

// Rendering system statistics
struct RenderSystemInfo {
    uint32_t drawCalls      = 0;
    uint32_t renderPasses   = 0;
    uint32_t textureBinds   = 0;
    uint32_t shaderSwitches = 0;
};

// Entity system statistics
struct EntitySystemInfo {
    uint32_t entityUpdates  = 0;
    uint32_t entityCreates  = 0;
    uint32_t entityDestroys = 0;
};

// Event system statistics
struct EventSystemInfo {
    uint32_t eventsDispatched   = 0;
    uint32_t eventsSubscribed   = 0;
    uint32_t eventsUnsubscribed = 0;
};

// Complete frame snapshot
struct FrameInfo {
    uint64_t frameIndex = 0;

    FrameRateInfo frameRateInfo;
    RenderSystemInfo renderSystemInfo;
    EntitySystemInfo entitySystemInfo;
    EventSystemInfo eventSystemInfo;
};
