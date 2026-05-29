/**
 * Screen-space lens flare (ghosts + halo + chromatic aberration).
 *
 * Classic image-space technique (Hennessy / Chapman): for any bright pixel,
 * the mirror around screen center produces a chain of "ghost" reflections
 * along the optical axis, plus a soft halo at a fixed radius from the
 * source. Inputs are the resolved HDR scene and a brightness threshold so
 * only the sun / emissives / bright specular contribute.
 *
 * Outputs the resolved scene color plus the flare contribution (black where
 * no flare is present): the pass renders into a fresh post-scratch target and
 * blits back into the resolved HDR, so it passes the scene through rather than
 * relying on an additive blend.
 */
#version 420 core

in vec2 vUV;

out vec4 FragColor;

uniform sampler2D u_hdr;
uniform float u_threshold;     // brightness floor (HDR units)
uniform float u_intensity;     // overall flare gain
uniform float u_chromatic;     // per-channel UV offset for chromatic aberration
uniform int   u_ghostCount;    // ghosts along the optical axis (1..8)
uniform float u_ghostSpacing;  // UV step between ghosts
uniform float u_haloRadius;    // UV distance from source to halo ring

// Aperture-blade starburst (procedural R8 texture, rotates with camera).
uniform sampler2D u_starburst;
uniform int   u_starburstEnabled;
uniform float u_starburstIntensity;
uniform float u_starburstRotation;

// Sample HDR and floor by a brightness threshold so dim pixels don't
// contribute. Keeps hue, attenuates magnitude.
vec3 sampleThresholded(vec2 uv) {
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return vec3(0.0);
    vec3 c = texture(u_hdr, uv).rgb;
    float l = max(max(c.r, c.g), c.b);
    return c * max(l - u_threshold, 0.0) / max(l, 1e-4);
}

// Sample the thresholded image with per-channel chromatic split along the
// vector toward screen center - emulates the rainbow fringe a real lens
// gives at extreme apertures.
vec3 sampleChromatic(vec2 uv, float chroma) {
    vec2 toCenter = vec2(0.5) - uv;
    vec2 dir = (length(toCenter) > 1e-5) ? normalize(toCenter) * chroma : vec2(0.0);
    vec3 c;
    c.r = sampleThresholded(uv + dir).r;
    c.g = sampleThresholded(uv          ).g;
    c.b = sampleThresholded(uv - dir).b;
    return c;
}

void main() {
    // The "ghost line" runs from the flipped UV (mirror around center) back
    // through screen center. Each ghost is one step along this line.
    vec2 flippedUV = vec2(1.0) - vUV;
    vec2 ghostDir = (vec2(0.5) - flippedUV);

    vec3 result = vec3(0.0);

    // Ghost chain. Per-ghost: chromatic split scales with index (further
    // ghosts get stronger fringe), and a soft tint walks gold->blue.
    int count = max(u_ghostCount, 1);
    for (int i = 0; i < count; ++i) {
        float t = (count > 1) ? float(i) / float(count - 1) : 0.5;
        vec2 ghostUV = flippedUV + ghostDir * (t * u_ghostSpacing);

        // Brighter near screen center, fade as the ghost wanders to edges.
        float d = length(vec2(0.5) - ghostUV);
        float edge = pow(max(1.0 - d * 2.0, 0.0), 2.0);

        vec3 g = sampleChromatic(ghostUV, u_chromatic * (0.5 + t));
        vec3 tint = mix(vec3(1.0, 0.85, 0.55), vec3(0.55, 0.75, 1.0), t);
        result += g * tint * edge;
    }

    // Halo: a single sample at a fixed distance from this fragment toward
    // screen center. Tightens into a ring when masked by distance to that
    // radius.
    vec2 toCenter = vec2(0.5) - vUV;
    float dCenter = length(toCenter);
    if (dCenter > 1e-5) {
        vec2 ndir = toCenter / dCenter;
        vec2 haloUV = vUV + ndir * u_haloRadius;
        vec3 h = sampleChromatic(haloUV, u_chromatic * 2.0);
        // Ring mask: peak around |dCenter - haloRadius| ~ 0.
        float ring = exp(-pow((dCenter - u_haloRadius) * 6.0, 2.0));
        vec3 halo = h * ring * 0.6;

        // Multiply the aperture-blade starburst into the halo. UVs are
        // rotated by u_starburstRotation (derived from camera yaw on the
        // CPU) so the spokes feel anchored to a physical lens. We rotate
        // around screen center.
        if (u_starburstEnabled == 1) {
            float ca = cos(u_starburstRotation);
            float sa = sin(u_starburstRotation);
            vec2 c = vUV - vec2(0.5);
            vec2 sUV = vec2(ca * c.x - sa * c.y, sa * c.x + ca * c.y) + vec2(0.5);
            float burst = texture(u_starburst, sUV).r;
            halo *= 1.0 + burst * u_starburstIntensity * 4.0;
        }
        result += halo;
    }

    // Pass the resolved scene through and add the flare on top - the pass
    // renders into a fresh scratch target, so this is a replace, not a blend.
    FragColor = vec4(texture(u_hdr, vUV).rgb + result * u_intensity, 1.0);
}
