/*
 * Local reflection probes, a cube-array layer each, in boxes. Blended over the sky smallest box
 * first, each covering only what the ones before it left: a room's probe wins inside the room
 * over the yard's around it, where an average would give each half. GLPass::bindAmbient sets the
 * count; the boxes and the arrays the backend binds once a frame.
 */

layout(binding = PROBE_SLOT_IRRADIANCE) uniform samplerCubeArray u_probeIrr;
layout(binding = PROBE_SLOT_PREFILTER)  uniform samplerCubeArray u_probePref;
uniform int u_probeCount;

struct ProbeEntry {
    vec4 center;    // xyz world centre, w pad
    vec4 extents;   // xyz half-extents, w pad
    vec4 params;    // x falloff, y intensity, z layer, w pad
};
layout(std140, binding = UBO_PROBES) uniform ProbeBlock {
    ProbeEntry probes[MAX_PROBES];
} u_probes;

// Parallax box correction: the direction from the box centre to where R leaves
// the box, so a cube captured at one point does not reflect as if at infinity.
vec3 probeParallax(vec3 R, vec3 worldPos, vec3 center, vec3 extents) {
    vec3  boxMin = center - extents;
    vec3  boxMax = center + extents;
    vec3  invR   = 1.0 / R;
    vec3  t1     = (boxMin - worldPos) * invR;
    vec3  t2     = (boxMax - worldPos) * invR;
    vec3  tFar   = max(t1, t2);
    float t      = min(min(tFar.x, tFar.y), tFar.z);
    return (worldPos + R * t) - center;
}

// 1 deep inside, fading to 0 at the face over falloff. Falloff 0 is held a thousandth wide:
// smoothstep's edges must differ.
float probeWeight(vec3 worldPos, vec3 center, vec3 extents, float falloff) {
    vec3  d = abs(worldPos - center) / max(extents, vec3(1e-3));
    float m = max(max(d.x, d.y), d.z);
    return 1.0 - smoothstep(1.0 - max(falloff, 1e-3), 1.0, m);
}

// Probe p's share of worldPos, of what the boxes before it left uncovered; adds it to covered.
// Coverage is the box's alone: the probe's intensity scales what it gives, not its share.
float probeShare(int p, vec3 worldPos, inout float covered) {
    ProbeEntry probe = u_probes.probes[p];
    float weight = probeWeight(worldPos, probe.center.xyz, probe.extents.xyz, probe.params.x);
    float share  = weight * (1.0 - covered);
    covered += max(share, 0.0);
    return share;
}

// The probes' irradiance over PI around n, blended over the sky's @p sky by their boxes.
vec3 probeIrradiance(vec3 worldPos, vec3 n, vec3 sky) {
    vec3  sum     = vec3(0.0);
    float covered = 0.0;
    for (int p = 0; p < u_probeCount && p < MAX_PROBES && covered < 0.999; ++p) {
        float share = probeShare(p, worldPos, covered);
        if (share <= 0.0) continue;
        vec4 at = vec4(n, u_probes.probes[p].params.z);
        sum += texture(u_probeIrr, at).rgb * (share * u_probes.probes[p].params.y);
    }
    return sum + sky * (1.0 - covered);
}
