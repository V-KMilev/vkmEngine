#include "statistics.h"

#include "frame_tracker.h"
#include "call_tracker.h"

// Lifecycle

StatisticTracker& StatisticTracker::get() {
    static StatisticTracker instance;
    return instance;
}

StatisticTracker::StatisticTracker()
    : m_frameTracker(std::make_unique<FrameTracker>())
    , m_callTracker(std::make_unique<CallTracker>())
{}

StatisticTracker::~StatisticTracker() = default;

void StatisticTracker::update() {
    m_frameTracker->update();

    m_frameInfo.frameIndex++;
    m_frameInfo.frameRateInfo = m_frameTracker->getFrameRateInfo();
    m_frameInfo.renderSystemInfo = m_callTracker->getRenderInfo();
    m_frameInfo.entitySystemInfo = m_callTracker->getEntityInfo();
    m_frameInfo.eventSystemInfo = m_callTracker->getEventInfo();

    m_callTracker->reset();
}

void StatisticTracker::reset() {
    m_frameTracker->reset();
    m_callTracker->reset();

    m_frameInfo = FrameInfo{};
}

// Recording (delegates to CallTracker)

void StatisticTracker::recordDrawCall() {
    m_callTracker->recordDrawCall();
}

void StatisticTracker::recordRenderPass() {
    m_callTracker->recordRenderPass();
}

void StatisticTracker::recordTextureBind() {
    m_callTracker->recordTextureBind();
}

void StatisticTracker::recordShaderSwitch() {
    m_callTracker->recordShaderSwitch();
}

void StatisticTracker::recordEntityUpdate() {
    m_callTracker->recordEntityUpdate();
}

void StatisticTracker::recordEntityCreate() {
    m_callTracker->recordEntityCreate();
}

void StatisticTracker::recordEntityDestroy() {
    m_callTracker->recordEntityDestroy();
}

void StatisticTracker::recordEventDispatch() {
    m_callTracker->recordEventDispatch();
}

void StatisticTracker::recordEventSubscribe() {
    m_callTracker->recordEventSubscribe();
}

void StatisticTracker::recordEventUnsubscribe() {
    m_callTracker->recordEventUnsubscribe();
}
