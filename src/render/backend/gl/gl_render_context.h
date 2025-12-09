#pragma once

#include <cstdint>

#include "render_context.h"

namespace Engine {
    class RenderView;
    class ResourceManager;
    class GLView;
}

namespace Core {
    class Context;
    class Shader;
}

namespace Engine {

/**
 * @brief OpenGL-specific render context passed to GL render passes.
 *
 * Contains GL-specific resources and state needed by GL passes.
 * This is separate from the generic pipeline to allow other backends
 * (Optix, CPU) to have their own context types.
 */
struct GLRenderContext : public RenderContext {
    GLView& glView;                      ///< OpenGL mesh resource manager
    Core::Context& glContext;            ///< OpenGL rendering context
    Core::Shader& shader;                ///< Default shader for forward rendering
};

} // namespace Engine
