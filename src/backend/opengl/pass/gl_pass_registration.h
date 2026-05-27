#pragma once

namespace Engine {

/**
 * @brief Populate RenderPassFactory with every builtin OpenGL pass.
 *
 * Call once at backend init (before composing the pipeline). Each pass
 * is registered under a stable string name that matches the value
 * returned by its @ref RenderPass::getName(), so the editor's pass
 * introspection and the factory share a single identifier vocabulary.
 *
 * The forward pass is registered as two distinct entries
 * ("GLForwardPass.Opaque" and "GLForwardPass.Transparent") because the
 * pipeline draws each phase separately.
 *
 * Pass builders look up their shader dependencies by canonical name on
 * the supplied ResourceManager (e.g. "shader:pbr", "shader:shadow"),
 * so the application has to register shaders before driving the factory.
 */
void registerBuiltinGLPasses();

} // namespace Engine
