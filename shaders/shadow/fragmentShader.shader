/**
 * Directional shadow depth shader - fragment stage.
 *
 * Depth-only: nothing to write. Depth is captured automatically into
 * the FBO's GL_DEPTH_ATTACHMENT. Kept as an explicit empty main() because
 * the Core::Shader loader requires both vertex and fragment files.
 */
#version 420 core

void main() {
}
