#pragma once

#include <GL/glew.h>

#include "gl_blit.h"

namespace Engine {

/**
 * @brief Shared envelope for "in-place fullscreen transform of the HDR scene".
 *
 * DoF / MotionBlur / LensFlare / SSR / TAA all share one shape: disable
 * depth/blend/cull, render the effect into an off-MSAA intermediate (the post
 * scratch, or the TAA write-history), then blit that color back into the HDR
 * resolve target and restore depth state. Only the middle (bind intermediate,
 * bind shader + uniforms + inputs, draw the screen triangle) differs per pass.
 *
 * beginFullscreenPost() sets the fullscreen render state; endFullscreenPost()
 * blits the intermediate back into the HDR resolve target and restores depth.
 * Templated on the context type so this header needs no GL-context include;
 * pass gl.getContext() at both ends.
 */
template <typename Context>
inline void beginFullscreenPost(Context& ctx) {
    ctx.setDepthTest(false);
    ctx.setDepthWrite(false);
    ctx.setBlending(false);
    ctx.setFaceCulling(false);
}

/**
 * @brief Blit the effect's intermediate color back into the HDR resolve
 *        target, then restore depth test + write. Pairs with
 *        beginFullscreenPost().
 *
 * @param ctx           GL state context (gl.getContext()).
 * @param srcFbo        FBO the effect rendered into (scratch / TAA write).
 * @param srcW,srcH     Its dimensions.
 * @param dstFbo        HDR resolve FBO to blit into (hdr.resolveFboId()).
 * @param dstW,dstH     Its dimensions.
 */
template <typename Context>
inline void endFullscreenPost(
    Context& ctx,
    unsigned int srcFbo, int srcW, int srcH,
    unsigned int dstFbo, int dstW, int dstH
) {
    Core::blitColor(srcFbo, dstFbo, srcW, srcH, dstW, dstH, GL_NEAREST);
    ctx.setDepthTest(true);
    ctx.setDepthWrite(true);
}

} // namespace Engine
