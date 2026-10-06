#pragma once

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
 */
void runGoldenTests(GLContext& gl, int& failures);

} // namespace Vkm::Test
