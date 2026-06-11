#include "gl_pass.h"

#include "gl_context.h"

namespace Engine {

void GLPass::beginFullscreen(Core::Context& gl) const {
    gl.setDepthTest(false);
    gl.setBlending(false);
    gl.setFaceCulling(false);
}

} // namespace Engine
