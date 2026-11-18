/*
 * Whether this frame's reflection trace met anything, at image unit 1 (GLReflectionPass::trace):
 * the trace marks it, and the color chain the hits read is filtered only on a frame that has one.
 */

layout(binding = 1, r32ui) uniform uimage2D u_found;

// Whether a chain level has nothing to build: no ray met the scene this frame.
bool chainUnread() {
    return imageLoad(u_found, ivec2(0)).r == 0u;
}
