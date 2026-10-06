#include "core/math/random.h"

#include <atomic>
#include <chrono>

namespace Vkm::Engine::Math::Random {

Rng& rng() {
    thread_local Rng t_generator = [] {
        static std::atomic<uint64_t> s_counter{0};
        const uint64_t n = s_counter.fetch_add(1, std::memory_order_relaxed);
        const uint64_t t = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()
        );
        return Rng(t ^ (n * 0x9E3779B97F4A7C15ULL), n + 1u);
    }();
    return t_generator;
}

} // namespace Vkm::Engine::Math::Random
