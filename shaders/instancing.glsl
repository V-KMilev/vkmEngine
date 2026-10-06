/*
 * Per-instance transforms, read through an object index into b_models. The index
 * is a divisor-1 attribute, so baseInstance offsets it per run with no per-draw
 * uniform.
 */
layout(location = ATTR_INSTANCE) in uint aInstance;

layout(std430, binding = SSBO_INSTANCE_MODELS) readonly buffer InstanceModels { mat4 b_models[]; };

mat4 instanceModel() { return b_models[aInstance]; }

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
