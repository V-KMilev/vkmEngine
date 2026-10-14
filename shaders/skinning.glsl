/*
 * Linear-blend skinning. The rig binding is a second vertex buffer (see GLMesh and animation.md,
 * "The GPU path"). Indices are 16-bit; weights are unorm8 cooked to sum to exactly 255, so no
 * stage renormalises.
 *
 * The including stage declares aPos first.
 */
layout(location = ATTR_BONES) in uvec4 aBones;    // into this rig's slice of the palette
layout(location = ATTR_WEIGHTS) in vec4  aWeights;  // sum to exactly 1.0

layout(std430, binding = SSBO_SKIN_PALETTE) readonly buffer SkinPalette { mat4 b_skin[]; };

mat4 skinMatrix(uint base) {
    return aWeights.x * b_skin[base + aBones.x]
        + aWeights.y * b_skin[base + aBones.y]
        + aWeights.z * b_skin[base + aBones.z]
        + aWeights.w * b_skin[base + aBones.w];
}

/*
 * `model` is the rig's world placement: skinned vertices are in rig-root space
 * (see importModelIntoScene).
 */
vec4 skinnedWorldPosition(mat4 model, uint base) {
    return model * (skinMatrix(base) * vec4(aPos, 1.0));
}
