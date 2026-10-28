/**
 * Irradiance volume - replace each refused probe by the blend of trusted ones its bake found.
 *
 * An invocation per refused probe. Which probes a hole fills from, and by how much, depends on
 * which were trusted - on where they stand, not on the light - so probeFillWeights finds it once
 * per bake, and every gather of every relight repairs its grid without reading it back.
 *
 * u_fill.data holds a target per refused probe, three words each - its cell, where its sources
 * begin in data, how many - and after them every source as two words: its cell and its weight's
 * bits. Cells count X fastest.
 */

layout(local_size_x = GROUP_FILL) in;

layout(std430, binding = SSBO_IRRADIANCE_FILL) readonly buffer FillBlock {
    uint data[];
} u_fill;

uniform int u_targetCount;

// Read and written: a target's sources are trusted probes, which no invocation writes.
layout(binding = 0, rgba16f) uniform image3D u_sh0;
layout(binding = 1, rgba16f) uniform image3D u_sh1;
layout(binding = 2, rgba16f) uniform image3D u_sh2;
layout(binding = 3, rgba16f) uniform image3D u_sh3;

ivec3 cellAt(uint index, ivec3 grid) {
    int i = int(index);
    return ivec3(i % grid.x, (i / grid.x) % grid.y, i / (grid.x * grid.y));
}

void main() {
    const uint target = gl_GlobalInvocationID.x;
    if (target >= uint(u_targetCount)) return;

    const ivec3 grid  = imageSize(u_sh0);
    const uint  first = u_fill.data[3u * target + 1u];
    const uint  count = u_fill.data[3u * target + 2u];

    vec3 sh0 = vec3(0.0);
    vec3 sh1 = vec3(0.0);
    vec3 sh2 = vec3(0.0);
    vec3 sh3 = vec3(0.0);
    for (uint k = 0u; k < count; ++k) {
        const ivec3 from   = cellAt(u_fill.data[first + 2u * k], grid);
        const float weight = uintBitsToFloat(u_fill.data[first + 2u * k + 1u]);
        sh0 += imageLoad(u_sh0, from).rgb * weight;
        sh1 += imageLoad(u_sh1, from).rgb * weight;
        sh2 += imageLoad(u_sh2, from).rgb * weight;
        sh3 += imageLoad(u_sh3, from).rgb * weight;
    }

    // Repaired, and no longer a verdict.
    const ivec3 cell = cellAt(u_fill.data[3u * target], grid);
    imageStore(u_sh0, cell, vec4(sh0, 1.0));
    imageStore(u_sh1, cell, vec4(sh1, 1.0));
    imageStore(u_sh2, cell, vec4(sh2, 1.0));
    imageStore(u_sh3, cell, vec4(sh3, 1.0));
}
