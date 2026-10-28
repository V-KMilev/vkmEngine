/*
 * Per-instance transforms, read through an object index into b_models. The index
 * is a divisor-1 attribute, so baseInstance offsets it per run with no per-draw
 * uniform. The including stage declares aPos first; SKINNED poses it by the bone palette.
 */
layout(location = ATTR_INSTANCE) in uint aInstance;

#include "objects.glsl"
#ifdef SKINNED
#include "skinning.glsl"
#include "skinning_instanced.glsl"
#endif

mat4 instanceModel() { return b_models[aInstance]; }

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
 * determinant, so callers normalise. The determinant's sign is put back, or a
 * mirrored instance's normals would face inward.
 */
mat3 normalMatrix(mat4 model) {
    const vec3 m0 = model[0].xyz;
    const vec3 m1 = model[1].xyz;
    const vec3 m2 = model[2].xyz;
    const vec3 c0 = cross(m1, m2);
    return mat3(c0, cross(m2, m0), cross(m0, m1)) * (dot(m0, c0) < 0.0 ? -1.0 : 1.0);
}
