/*
 * Screen-space shadows for the cascaded sun: what the cascades are too coarse to catch - a foot on
 * the floor, a pebble, the crease under a ledge - found by marching the depth buffer toward the
 * sun, into the AO target's alpha.
 *
 * A port of Bend Studio's screen-space shadows (Days Gone; SIGGRAPH 2023), bend_sss_gpu.h:
 *
 *   Copyright 2023 Sony Interactive Entertainment.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of the License at
 *   http://www.apache.org/licenses/LICENSE-2.0 (and in LICENSE-APACHE beside this file).
 *   Unless required by applicable law or agreed to in writing, software distributed under the
 *   License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 *   either express or implied. See the License for the specific language governing permissions
 *   and limitations under the License.
 *
 * Changed from the original: GLSL; reversed depth (near 1, far 0) hard-coded; texelFetch with a
 * bounds test for its clamp-to-border sampler; a shared flag for its wave-intrinsic early-out; the
 * viewmodel's depth band read as far; an orthographic camera's span fixed; a thicker
 * surface, with edge pixels casting nothing; a reach in metres at the pixel's depth; the result
 * written into one channel of an existing target.
 *
 * Each group is one wavefront of WAVE_SIZE pixels in a line toward the light. Its depths go to
 * shared memory, divided by their screen distance to the light so every ray is parallel there;
 * each pixel then compares the pixels CONTACT_REACH spans along its ray, between
 * HARD_SHADOW_SAMPLES + FADE_OUT_SAMPLES and SAMPLE_COUNT, to its own. No noise and no history: a
 * still frame shades the same every time (docs/guides/engine.md section 4).
 */
layout(local_size_x = GROUP_SCREEN_SHADOW) in;

const int WAVE_SIZE           = GROUP_SCREEN_SHADOW;
const int SAMPLE_COUNT        = 60;  // the most pixels a shadow reaches
const int HARD_SHADOW_SAMPLES = 4;   // the nearest, each able to shadow the pixel alone
const int FADE_OUT_SAMPLES    = 8;   // the farthest within the reach, faded so it has no hard edge

const int READ_COUNT          = SAMPLE_COUNT / WAVE_SIZE + 2;

// How far toward the light a contact reaches, in metres: the pass grounds what the cascades are
// too coarse for, and a reach fixed in pixels would draw a far thin thing's shadow long and solid.
const float CONTACT_REACH = 0.3;

// What a sample's depth stands for behind its surface, as a share of its depth's distance to the
// far plane: a sample up to about twice this share of the view distance in front of a ray shadows
// it. Any thinner, and a solid foot's shadow is only its rim.
const float SURFACE_THICKNESS  = 0.02;
// A depth step past this share of the same distance is an edge: read unblended, and casting nothing,
// so a flat surface seen at a grazing angle does not stripe itself.
const float BILINEAR_THRESHOLD = 0.08;
// How sharply a near miss turns to shadow: 1 is linear, higher closes the gap.
const float SHADOW_CONTRAST    = 4.0;

const float Z_SIGN = -1.0;  // the far plane is the smaller depth

#include "../depth.glsl"   // isFarDepth
#include "../camera.glsl"  // inViewmodelBand

layout(binding = POST_SLOT_SCENE_DEPTH) uniform sampler2D u_sceneDepth;
// The AO target (scene_ao.glsl): its alpha is written, the rest kept.
layout(binding = 0, rgba8) uniform image2D u_target;

// From GLScreenShadowPass: the light's pixel (xy), its depth (z), and whether rays run toward it
// (w = +1) or away (w = -1).
uniform vec4  u_light;
uniform ivec2 u_waveOffset;  // this dispatch's first wave, in pixels from the light
// The depth span a thickness is a share of, for an orthographic camera, whose depth is linear;
// 0 for a perspective one, whose span is each depth's distance to the far plane.
uniform float u_orthoSpan;

shared float s_depth[READ_COUNT * WAVE_SIZE];
shared uint  s_anyShaded;

// The scene's depth, far off the screen and in the viewmodel's band: neither casts on the world.
float fetchDepth(ivec2 texel) {
    if (any(lessThan(texel, ivec2(0))) || any(greaterThanEqual(texel, textureSize(u_sceneDepth, 0)))) {
        return FAR_DEPTH;
    }
    const float depth = texelFetch(u_sceneDepth, texel, 0).r;
    return inViewmodelBand(depth) ? FAR_DEPTH : depth;
}

// The depth span a thickness or an edge threshold is a share of.
float thicknessScale(float depth) {
    return u_orthoSpan > 0.0 ? u_orthoSpan : abs(FAR_DEPTH - depth);
}

// Where this thread's first pixel lies, the step to the one WAVE_SIZE further along the ray, the
// first pixel's distance to the light, and whether the ray runs mostly along x.
void wavefrontExtents(out vec2 deltaXY, out vec2 pixelXY, out float pixelDistance, out bool xMajor) {
    ivec2 xy = ivec2(gl_WorkGroupID.yz) * WAVE_SIZE + u_waveOffset;

    const vec2 lightXY       = floor(u_light.xy) + 0.5;
    const vec2 lightFraction = u_light.xy - lightXY;
    const bool reverse       = u_light.w > 0.0;

    const ivec2 signXY    = ivec2(sign(vec2(xy)));
    const bool horizontal = abs(xy.x + signXY.y) < abs(xy.y - signXY.x);
    const ivec2 axis      = horizontal ? ivec2(signXY.y, 0) : ivec2(0, -signXY.x);
    xy += axis * int(gl_WorkGroupID.x);
    const vec2 xyF = vec2(xy);

    // Only the major axis matters for reaching the light's centre.
    xMajor = abs(xyF.x) > abs(xyF.y);
    const float major      = xMajor ? xyF.x : xyF.y;
    const float majorStart = abs(major);
    const float majorEnd   = abs(major) - float(WAVE_SIZE);
    float majorFraction    = xMajor ? lightFraction.x : lightFraction.y;
    majorFraction          = major > 0.0 ? -majorFraction : majorFraction;

    // The innermost ring ends at a pixel centre, so rounding to pixels skips none.
    const vec2 startXY = xyF + lightXY;
    const vec2 endXY   = mix(u_light.xy, startXY, (majorEnd + majorFraction) / (majorStart + majorFraction));
    deltaXY = startXY - endXY;

    const float threadStep = float(int(gl_LocalInvocationID.x) ^ (reverse ? 0 : WAVE_SIZE - 1));
    pixelXY       = mix(startXY, endXY, threadStep / float(WAVE_SIZE));
    pixelDistance = majorStart - threadStep + majorFraction;
}

void main() {
    vec2  deltaXY;
    vec2  pixelXY;
    float pixelDistance;
    bool  xMajor;
    wavefrontExtents(deltaXY, pixelXY, pixelDistance, xMajor);

    const float direction = -u_light.w;
    const ivec2 writeXY   = ivec2(floor(pixelXY));

    float samplingDepth[READ_COUNT];
    float shadowingDepth[READ_COUNT];
    float depthScale[READ_COUNT];
    float sampleDistance[READ_COUNT];

    for (int i = 0; i < READ_COUNT; ++i) {
        // Two reads a pixel, blended across the ray's minor axis unless they straddle an edge;
        // the major axis stays on pixel centres.
        const ivec2 readXY   = ivec2(floor(pixelXY));
        const float minor    = xMajor ? pixelXY.y : pixelXY.x;
        const float bilinear = fract(minor) - 0.5;
        const int   bias     = bilinear > 0.0 ? 1 : -1;
        const ivec2 offset   = ivec2(xMajor ? 0 : bias, xMajor ? bias : 0);
        const vec2  depths   = vec2(fetchDepth(readXY), fetchDepth(readXY + offset));

        depthScale[i] = thicknessScale(depths.x);
        const bool edge = abs(depths.x - depths.y) > depthScale[i] * BILINEAR_THRESHOLD;

        // A pixel starts from its own depth, but casts from the farther of the blend's two, since
        // a ray past it may read either; an edge casts nothing.
        samplingDepth[i]  = depths.x;
        shadowingDepth[i] = edge ? 1e20 : depths.x + abs(depths.x - depths.y) * Z_SIGN;

        sampleDistance[i] = pixelDistance + float(WAVE_SIZE * i) * direction;
        pixelXY += deltaXY * direction;
    }

    // The sky needs no shadow; a group of nothing but sky leaves at once. Every thread reaches
    // the barriers, so the return is the whole group's.
    const bool skip = isFarDepth(samplingDepth[0]) || samplingDepth[0] >= NEAR_DEPTH;
    if (gl_LocalInvocationIndex == 0u) s_anyShaded = 0u;
    barrier();
    if (!skip) atomicOr(s_anyShaded, 1u);
    barrier();
    if (s_anyShaded == 0u) return;

    for (int i = 0; i < READ_COUNT; ++i) {
        // Divided by the distance to the light, every ray toward it is parallel.
        float stored = (shadowingDepth[i] - u_light.z) / sampleDistance[i];
        // Past the light, a pixel near it would read samples beyond: they count for nothing.
        if (i != 0 && sampleDistance[i] <= 0.0) stored = 1e10;
        s_depth[i * WAVE_SIZE + int(gl_LocalInvocationID.x)] = stored;
    }
    barrier();
    if (skip) return;

    // Contacts only: as many pixels as CONTACT_REACH spans at this depth, within the samples' bounds.
    const float viewDepth = cameraIsPerspective()
        ? linearSceneDepth(samplingDepth[0], u_camera.invProjection)
        : 1.0;
    const float perMetre  = u_camera.projection[1][1] * 0.5 * float(imageSize(u_target).y) / viewDepth;
    const int   reach     = int(clamp(
        CONTACT_REACH * perMetre,
        float(HARD_SHADOW_SAMPLES + FADE_OUT_SAMPLES),
        float(SAMPLE_COUNT)
    ));

    // Scaled so the shadow window is one wide: the projection's divide undone, the thickness a
    // share of this depth's span, and a minimum width close to the light.
    const float scale = min(sampleDistance[0] + direction, 1.0 / SURFACE_THICKNESS)
        * sampleDistance[0] / depthScale[0];
    const float start = ((samplingDepth[0] - u_light.z) / sampleDistance[0]) * scale - Z_SIGN;

    const int first = int(gl_LocalInvocationID.x) + 1;
    float hard   = 1.0;
    vec4  shadow = vec4(1.0);

    // The nearest samples: one alone can shadow the pixel, so a contact is grounded.
    for (int i = 0; i < HARD_SHADOW_SAMPLES; ++i) {
        hard = min(hard, abs(start - s_depth[first + i] * scale));
    }
    // The rest, into four interleaved minima averaged below, so one sample cannot shadow alone.
    for (int i = HARD_SHADOW_SAMPLES; i < reach - FADE_OUT_SAMPLES; ++i) {
        shadow[i & 3] = min(shadow[i & 3], abs(start - s_depth[first + i] * scale));
    }
    for (int i = reach - FADE_OUT_SAMPLES; i < reach; ++i) {
        const float fade = float(i + 1 - (reach - FADE_OUT_SAMPLES)) / float(FADE_OUT_SAMPLES + 1) * 0.75;
        shadow[i & 3] = min(shadow[i & 3], abs(start - s_depth[first + i] * scale) + fade);
    }

    // A sample on the ray's depth is zero; the contrast lets a near miss shadow too.
    shadow = clamp(shadow * SHADOW_CONTRAST + (1.0 - SHADOW_CONTRAST), 0.0, 1.0);
    hard   = clamp(hard * SHADOW_CONTRAST + (1.0 - SHADOW_CONTRAST), 0.0, 1.0);
    const float lit = min(hard, dot(shadow, vec4(0.25)));

    if (any(lessThan(writeXY, ivec2(0))) || any(greaterThanEqual(writeXY, imageSize(u_target)))) return;
    const vec4 kept = imageLoad(u_target, writeXY);
    imageStore(u_target, writeXY, vec4(kept.rgb, lit));
}
