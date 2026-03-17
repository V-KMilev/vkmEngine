#include <thread>
#include <chrono>

void wait(size_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

#include <random>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "platform/threading/thread_pool.h"

thread_local std::mt19937 tls_rng{std::random_device{}()};
thread_local std::uniform_real_distribution<float> tls_dist(0.0f, 1.0f);

inline float randomFloat() { return tls_dist(tls_rng); }

using Clock = std::chrono::high_resolution_clock;

#define TRACE_BEGIN(TAG) \
    auto _start_##TAG = Clock::now();

#define TRACE_END(TAG) \
    auto _end_##TAG = Clock::now();

#define TRACE_GET_TIME(TAG) \
    std::chrono::duration_cast<std::chrono::microseconds>(_end_##TAG - _start_##TAG).count()


// Minimum squared extent for a valid AABB. glm::epsilon (~1.19e-7) is too small
// for world-space coordinates in range [-1000, 1000]. 1e-4 squared = 1e-8.
inline constexpr float BOUNDS_EPSILON_SQ = 1e-8f;

/**
 * @brief True if the AABB has non-degenerate extent (squared length of extent > epsilon).
 * Degenerate or empty bounds return false. Uses squared extent to avoid sqrt.
 */
inline bool hasValidBounds(const glm::vec3& min, const glm::vec3& max) noexcept {
    const glm::vec3 extent = max - min;
    return glm::dot(extent, extent) > BOUNDS_EPSILON_SQ;
}

/**
 * @brief Transform an AABB from model space to world space using Arvo's method.
 *
 * Uses algebraic AABB transformation instead of transforming 8 corners.
 * ~7x faster: 18 scalar muls vs 128 for corner-based approach.
 *
 * @param matrix Model-to-world matrix.
 * @param localMin Minimum corner in model space.
 * @param localMax Maximum corner in model space.
 * @param[out] worldMin Output minimum in world space.
 * @param[out] worldMax Output maximum in world space.
 */
inline void localToWorldAABB(
    const glm::mat4& matrix,
    const glm::vec3& localMin,
    const glm::vec3& localMax,
    glm::vec3& worldMin,
    glm::vec3& worldMax
) {
    // Start with translation component
    worldMin = glm::vec3(matrix[3]);
    worldMax = glm::vec3(matrix[3]);

    // For each matrix column (x, y, z basis vectors)
    for (int j = 0; j < 3; ++j) {
        const glm::vec3 col(matrix[j]);
        const glm::vec3 a = col * localMin[j];
        const glm::vec3 b = col * localMax[j];
        worldMin += glm::min(a, b);
        worldMax += glm::max(a, b);
    }
}
void work() {
    // Generate a glm::mat4 filled with random values on each call
    glm::mat4 matrix;
    glm::vec3 localMin, localMax;
    glm::vec3 worldMin, worldMax;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            matrix[col][row] = randomFloat();
        }
    }

    // Generate random localMin and localMax vectors with localMin <= localMax componentwise
    for (int i = 0; i < 3; ++i) {
        float a = randomFloat() * 10.0f; // range [0,10)
        float b = randomFloat() * 10.0f;
        if (a < b) {
            localMin[i] = a;
            localMax[i] = b;
        } else {
            localMin[i] = b;
            localMax[i] = a;
        }
    }

    if (!hasValidBounds(localMin, localMax)) {
        return;
    }

    localToWorldAABB(matrix, localMin, localMax, worldMin, worldMax);
}

int main() {
    size_t iterations = 100;
    size_t taskCount = 1000000;

    long long avgSequentialTime = 0;
    long long avgThreadPoolTime = 0;

    for (size_t i = 0; i < iterations; ++i) {

        TRACE_BEGIN(sequential)
        for (size_t i = 0; i < taskCount; ++i) {
            work();
        }
        TRACE_END(sequential)

        TRACE_BEGIN(threadpool)
        Engine::parallelFor(taskCount, work);
        TRACE_END(threadpool)

        auto sequentialTime = TRACE_GET_TIME(sequential);
        auto threadpoolTime = TRACE_GET_TIME(threadpool);

        avgSequentialTime += sequentialTime;
        avgThreadPoolTime += threadpoolTime;

        printf("Sequential Time: %lld us\n", static_cast<long long>(sequentialTime));
        printf("ThreadPool Time: %lld us\n", static_cast<long long>(threadpoolTime));
        printf("Speedup: %.4fx\n", static_cast<double>(sequentialTime) / threadpoolTime);
    }

    avgSequentialTime /= iterations;
    avgThreadPoolTime /= iterations;

    printf("Average Sequential Time: %lld us\n", static_cast<long long>(avgSequentialTime));
    printf("Average ThreadPool Time: %lld us\n", static_cast<long long>(avgThreadPoolTime));
    printf("Speedup: %.4fx\n", static_cast<double>(avgSequentialTime) / avgThreadPoolTime);

    return 0;
}