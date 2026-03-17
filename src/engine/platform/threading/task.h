#pragma once

#include <functional>

namespace Engine {

struct Task {
    Task() : m_function(nullptr) {}
    Task(std::function<void()> && function) : m_function(std::move(function)) {}

    operator bool() const {
        return m_function != nullptr;
    }

    void execute() {
        m_function();
    }

    std::function<void()> m_function = nullptr;
};

} // namespace Engine