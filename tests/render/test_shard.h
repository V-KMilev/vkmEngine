#pragma once

#include <cstddef>

namespace Vkm::Test {

/**
 * @brief Which of the GPU suite's checks a run takes: all of them, or every count-th from index,
 *        so a slow device can split the suite across machines (`vkm_render_tests --shard i/n`).
 */
struct Shard {
    int index = 0;  ///< This run's, from zero.
    int count = 1;  ///< How many runs split the suite.

    /// Whether this run takes the @p check-th of a list.
    bool takes(std::size_t check) const {
        return check % static_cast<std::size_t>(count) == static_cast<std::size_t>(index);
    }

    /// Whether this run takes what is not split: the first does.
    bool first() const { return index == 0; }
};

} // namespace Vkm::Test
