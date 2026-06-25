#pragma once

#include <functional>

namespace Engine {

/** @brief A unit of work for the ThreadPool: a type-erased nullary callable. */
struct Task {
    std::function<void()> function = nullptr;

    Task() = default;
    Task(std::function<void()> && fn) : function(std::move(fn)) {}

    void execute() {
        function();
    }
};

} // namespace Engine
