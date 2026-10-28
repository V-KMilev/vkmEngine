#pragma once

#include "test_shard.h"

namespace Vkm::Test {

class GLContext;

/**
 * @brief Render fixed scenes through the whole backend and compare each with this GPU's golden.
 *
 * Gives @p gl a pbuffer surface first, since the frame ends on the default
 * framebuffer; a device with none skips.
 *
 * @param gl       The suite's context.
 * @param failures Incremented once per mismatching frame.
 * @param shard    Which of the checks this run takes.
 */
void runGoldenTests(GLContext& gl, int& failures, const Shard& shard);

} // namespace Vkm::Test
