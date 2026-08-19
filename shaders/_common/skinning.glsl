/*
 * Linear-blend skinning: the per-vertex rig binding, the frame's bone palettes,
 * and the one expression that turns them into a world position.
 *
 * The rig binding is a second vertex buffer at divisor 0, not four more fields
 * on Vertex - see GLMesh::update. Indices are 16-bit so no format carries a
 * 255-bone ceiling; weights are unorm8 quantised at cook time to sum to exactly
 * 255, so `w / 255.0` sums to exactly 1.0 and no stage here renormalises.
 *
 * The including stage declares aPos before including this file, the way
 * instancing_normal.glsl reads the aInstance its own sibling declares.
 */

layout (location = 8) in uvec4 aBones;    // indices into this rig's slice of the palette
layout (location = 9) in vec4  aWeights;  // unorm8 influences, summing to exactly 1.0

layout(std430, binding = 5) readonly buffer SkinPalette { mat4 b_skin[]; };

mat4 skinMatrix(uint base) {
    return aWeights.x * b_skin[base + aBones.x]
         + aWeights.y * b_skin[base + aBones.y]
         + aWeights.z * b_skin[base + aBones.z]
         + aWeights.w * b_skin[base + aBones.w];
}

/*
 * Returning the world POSITION rather than the matrix is what makes the
 * prepass / forward depth agreement structural. The forward pass draws against
 * the depth the prepass primed, with LEQUAL and writes off, so the two programs
 * must compute gl_Position identically - and here there is only one expression
 * for them to compute it from.
 *
 * The model matrix is the rig's world placement: skinned vertices resolve into
 * rig-root space, which is why import parents skinned meshes to the rig at
 * identity.
 */
vec4 skinnedWorldPosition(mat4 model, uint base) {
    return model * (skinMatrix(base) * vec4(aPos, 1.0));
}
