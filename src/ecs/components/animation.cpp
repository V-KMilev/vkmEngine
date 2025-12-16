#include "animation.h"

namespace Engine {

Animation::Animation(uint32_t id
) : Component(id, ComponentType::Animation),
    m_time(0.0f),
    m_speed(1.0f),
    m_playing(true),
    m_looping(true) {}

void Animation::update(float deltaTime) {
    if (!m_playing) {
        return;
    }

    m_time += deltaTime * m_speed;
    float duration = getDuration();

    if (duration > 0.0f && m_time >= duration) {
        if (m_looping) {
            m_time = std::fmod(m_time, duration);
        } else {
            m_time = duration;
            m_playing = false;
        }
    }
}

} // namespace Engine

