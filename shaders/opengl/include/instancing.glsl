/*
 * Per-instance transforms, read through an object index into b_models. The instance
 * is a divisor-1 attribute, so baseInstance offsets it per run with no per-draw
 * uniform: its object's index, and above INSTANCE_OBJECT_BITS how the eye's draw
 * fades it (fade.glsl). The including stage declares aPos first; SKINNED poses it by
 * the bone palette.
 */
layout(location = ATTR_INSTANCE) in uint aInstance;

// The object the instance draws.
uint instanceObject() { return aInstance & INSTANCE_OBJECT_MASK; }

// How the instance fades, for fade.glsl's fadeCover; zero draws it whole.
uint instanceFade() { return aInstance & ~INSTANCE_OBJECT_MASK; }

#include "objects.glsl"
#ifdef SKINNED
/*
 * Linear-blend skinning. The rig binding is a second vertex buffer (see GLMesh and animation.md,
 * "The GPU path"). Indices are 16-bit; weights are unorm8 cooked to sum to exactly 255, so no
 * stage renormalizes.
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

/*
 * Each object's first bone in the frame's palette, indexed by instanceObject like
 * the models.
 */
layout(std430, binding = SSBO_INSTANCE_SKIN_BASE) readonly buffer InstanceSkinBase { uint b_skinBase[]; };

uint instanceSkinBase() { return b_skinBase[instanceObject()]; }
#endif

mat4 instanceModel() { return b_models[instanceObject()]; }

// Where the instance's vertex lies in the world: one expression, so the prepass and the forward
// pass agree to the bit under GLDepth::EYE_TEST_EQUAL.
vec4 instanceWorldPosition() {
#ifdef SKINNED
    return skinnedWorldPosition(instanceModel(), instanceSkinBase());
#else
    return instanceModel() * vec4(aPos, 1.0);
#endif
}

/*
 * The cofactor of `model`'s upper 3x3: transpose(inverse()) scaled by the
 * determinant, so callers normalize. The determinant's sign is put back, or a
 * mirrored instance's normals would face inward.
 */
mat3 normalMatrix(mat4 model) {
    const vec3 m0 = model[0].xyz;
    const vec3 m1 = model[1].xyz;
    const vec3 m2 = model[2].xyz;
    const vec3 c0 = cross(m1, m2);
    return mat3(c0, cross(m2, m0), cross(m0, m1)) * (dot(m0, c0) < 0.0 ? -1.0 : 1.0);
}
