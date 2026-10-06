#include "platform/input/input_handle.h"

#include <algorithm>
#include <iterator>

namespace Vkm::Engine {

void InputHandle::onKeyEvent(int key, bool pressed) {
    if (key < 0 || key > MAX_KEY) return;
    // A repeat is a press of a key already down: typed again, not struck.
    if (pressed && !m_keyState[key]) m_keyStruck[key] = true;
    if (pressed)                     m_keyTyped[key]  = true;
    m_keyState[key] = pressed;
}

void InputHandle::onText(char32_t codepoint) {
    m_text.push_back(codepoint);
}

bool InputHandle::isKeyPressed(int key) const {
    if (key < 0 || key > MAX_KEY) return false;
    return m_keyState[key];
}

bool InputHandle::wasKeyStruck(int key) const {
    if (key < 0 || key > MAX_KEY) return false;
    return m_keyStruck[key];
}

bool InputHandle::wasKeyTyped(int key) const {
    if (key < 0 || key > MAX_KEY) return false;
    return m_keyTyped[key];
}

void InputHandle::clearStrikes() {
    std::fill(std::begin(m_keyStruck), std::end(m_keyStruck), false);
    std::fill(std::begin(m_keyTyped), std::end(m_keyTyped), false);
    std::fill(std::begin(m_buttonStruck), std::end(m_buttonStruck), false);
    m_text.clear();
}

void InputHandle::moveTo(double x, double y) {
    m_deltaX = x - m_x;
    m_deltaY = y - m_y;
    m_x = x;
    m_y = y;
}

void InputHandle::setButton(int button, bool pressed) {
    if (button < 0 || button > MAX_MOUSE_BUTTON) return;
    if (pressed && !m_buttonState[button]) m_buttonStruck[button] = true;
    m_buttonState[button] = pressed;
}

bool InputHandle::isButtonPressed(int button) const {
    if (button < 0 || button > MAX_MOUSE_BUTTON) return false;
    return m_buttonState[button];
}

bool InputHandle::wasButtonStruck(int button) const {
    if (button < 0 || button > MAX_MOUSE_BUTTON) return false;
    return m_buttonStruck[button];
}

void InputHandle::addScroll(double yOffset) {
    m_scrollY += yOffset;
}

void InputHandle::resetScrollDelta() {
    m_scrollY = 0.0;
}

} // namespace Vkm::Engine
