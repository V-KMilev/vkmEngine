#include "debug/call_tracker.h"

namespace Engine {

// Render system recording

void CallTracker::recordDrawCall() {
    m_render.drawCalls.fetch_add(1, std::memory_order_relaxed);
}

void CallTracker::recordRenderPass() {
    m_render.renderPasses.fetch_add(1, std::memory_order_relaxed);
}

void CallTracker::recordTextureBind() {
    m_render.textureBinds.fetch_add(1, std::memory_order_relaxed);
}

void CallTracker::recordShaderSwitch() {
    m_render.shaderSwitches.fetch_add(1, std::memory_order_relaxed);
}

// Entity system recording

void CallTracker::recordEntityUpdate() {
    m_entity.entityUpdates.fetch_add(1, std::memory_order_relaxed);
}

void CallTracker::recordEntityCreate() {
    m_entity.entityCreates.fetch_add(1, std::memory_order_relaxed);
}

void CallTracker::recordEntityDestroy() {
    m_entity.entityDestroys.fetch_add(1, std::memory_order_relaxed);
}

// Event system recording

void CallTracker::recordEventDispatch() {
    m_event.eventsDispatched.fetch_add(1, std::memory_order_relaxed);
}

void CallTracker::recordEventSubscribe() {
    m_event.eventsSubscribed.fetch_add(1, std::memory_order_relaxed);
}

void CallTracker::recordEventUnsubscribe() {
    m_event.eventsUnsubscribed.fetch_add(1, std::memory_order_relaxed);
}

// Reset

void CallTracker::reset() {
    m_render.drawCalls.store(0, std::memory_order_relaxed);
    m_render.renderPasses.store(0, std::memory_order_relaxed);
    m_render.textureBinds.store(0, std::memory_order_relaxed);
    m_render.shaderSwitches.store(0, std::memory_order_relaxed);

    m_entity.entityUpdates.store(0, std::memory_order_relaxed);
    m_entity.entityCreates.store(0, std::memory_order_relaxed);
    m_entity.entityDestroys.store(0, std::memory_order_relaxed);

    m_event.eventsDispatched.store(0, std::memory_order_relaxed);
    m_event.eventsSubscribed.store(0, std::memory_order_relaxed);
    m_event.eventsUnsubscribed.store(0, std::memory_order_relaxed);
}

// Snapshots

RenderSystemInfo CallTracker::getRenderInfo() const {
    return {
        m_render.drawCalls.load(std::memory_order_relaxed),
        m_render.renderPasses.load(std::memory_order_relaxed),
        m_render.textureBinds.load(std::memory_order_relaxed),
        m_render.shaderSwitches.load(std::memory_order_relaxed)
    };
}

EntitySystemInfo CallTracker::getEntityInfo() const {
    return {
        m_entity.entityUpdates.load(std::memory_order_relaxed),
        m_entity.entityCreates.load(std::memory_order_relaxed),
        m_entity.entityDestroys.load(std::memory_order_relaxed)
    };
}

EventSystemInfo CallTracker::getEventInfo() const {
    return {
        m_event.eventsDispatched.load(std::memory_order_relaxed),
        m_event.eventsSubscribed.load(std::memory_order_relaxed),
        m_event.eventsUnsubscribed.load(std::memory_order_relaxed)
    };
}

} // namespace Engine
