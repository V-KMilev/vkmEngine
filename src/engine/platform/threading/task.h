#pragma once

#include <functional>

namespace Engine {

struct Task {
    std::function<void()> function = nullptr;

    Task() = default;
    Task(std::function<void()> && fn) : function(std::move(fn)) {}

    void execute() {
        function();
    }
};

} // namespace Engine
