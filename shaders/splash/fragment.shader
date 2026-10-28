/*
 * Splash fragment: one logo, centred over black, and under it at the screen's foot the line of its
 * caption, if it has one, in grey. An ident's logo is polished dark metal in the dark, bevelled at
 * its edges, seen only by a comet's light - a point light, itself unseen - shaded as a metal is
 * (GGX, Fresnel). The comet comes in from the screen's top left down the curve y = 1/x, in front of
 * the logo along its lettering to the last E; turning back round the big V it goes behind, sweeps out
 * to the bottom right and arcs up into the middle of the M, which it strikes from behind. In front,
 * its light shows the metal; behind, it streams round the logo through a faint haze, the logo's shadow
 * cut into the rays. The strike flashes and sends a ring of light out across the metal, and from there
 * the logo heats until it glows red hot, shimmering, giving its light off into the air.
 *
 * The draw covers the screen, so it is also the clear. u_rect and u_captionRect map each image into
 * the triangle's 0..1 space (xy = origin, zw = size), keeping its proportions on any surface. Each
 * is a white mark on transparency, so its alpha is the coverage and u_opacity the fade.
 */

#include "../color.glsl"
#include "../constants.glsl"
#include "../noise.glsl"

in vec2 vUV;

out vec4 FragColor;

layout(binding = SPLASH_SLOT_LOGO) uniform sampler2D u_logo;
layout(binding = SPLASH_SLOT_SHAPE) uniform sampler2D u_shape;  // how far inside the logo, in its heights
layout(binding = SPLASH_SLOT_GLOW) uniform sampler2D u_glow;    // its coverage blurred, near and wide
layout(binding = SPLASH_SLOT_CAPTION) uniform sampler2D u_caption;
uniform vec4  u_rect;
uniform vec4  u_captionRect;
uniform float u_opacity;
uniform float u_captionOpacity;  // the caption's own fade, under u_opacity
uniform int   u_hasLogo;
uniform int   u_hasCaption;
uniform float u_logoAspect;  // the logo's width over its height
uniform vec4  u_glowRect;    // the logo's uv to u_glow's: xy a scale, zw an offset
uniform int   u_ident;       // 1: the logo is an ident's, lit as above; 0: shown as it is
uniform float u_comet;       // how far the comet is along its path, by length, 0..1; none outside
uniform float u_struck;      // seconds since it struck, below zero before
uniform float u_heat;        // how hot the logo has grown, 0..1
uniform float u_time;        // seconds into the splash, for the heat's shimmer

// How bright the caption's line is beside the logo, linear.
const float CAPTION_TONE = 0.3;
// The mip bias an image drawn far smaller than it is is read with: finer than its size would pick.
const float SHARPEN = -1.5;

// The comet's path (see the top), at even steps along its length: x from the screen's middle and y
// up from its foot, in screen heights; z its height over the logo's face in LIGHT_HEIGHTs, below zero
// behind it. When it is where along it is the pass's to say.
const int  PATH_POINTS = 161;
const vec3 PATH[PATH_POINTS] = vec3[PATH_POINTS](
    vec3(-0.8889, 0.9900, -2.00), vec3(-0.8827, 0.9666, -2.00), vec3(-0.8761, 0.9433, -2.00),
    vec3(-0.8690, 0.9202, -2.00), vec3(-0.8615, 0.8972, -2.00), vec3(-0.8535, 0.8744, -2.00),
    vec3(-0.8449, 0.8517, -2.00), vec3(-0.8359, 0.8293, -1.99), vec3(-0.8262, 0.8071, -1.98),
    vec3(-0.8161, 0.7851, -1.95), vec3(-0.8053, 0.7635, -1.92), vec3(-0.7939, 0.7421, -1.88),
    vec3(-0.7818, 0.7211, -1.83), vec3(-0.7691, 0.7005, -1.77), vec3(-0.7558, 0.6804, -1.70),
    vec3(-0.7418, 0.6606, -1.63), vec3(-0.7271, 0.6414, -1.55), vec3(-0.7117, 0.6227, -1.46),
    vec3(-0.6956, 0.6046, -1.36), vec3(-0.6789, 0.5872, -1.26), vec3(-0.6615, 0.5703, -1.16),
    vec3(-0.6435, 0.5542, -1.05), vec3(-0.6249, 0.5387, -0.93), vec3(-0.6057, 0.5240, -0.81),
    vec3(-0.5859, 0.5100, -0.70), vec3(-0.5657, 0.4968, -0.58), vec3(-0.5449, 0.4844, -0.46),
    vec3(-0.5237, 0.4727, -0.34), vec3(-0.5021, 0.4617, -0.22), vec3(-0.4802, 0.4515, -0.10),
    vec3(-0.4579, 0.4421, 0.01), vec3(-0.4354, 0.4333, 0.12), vec3(-0.4126, 0.4252, 0.22),
    vec3(-0.3895, 0.4178, 0.32), vec3(-0.3663, 0.4111, 0.41), vec3(-0.3429, 0.4049, 0.50),
    vec3(-0.3194, 0.3993, 0.58), vec3(-0.2957, 0.3943, 0.66), vec3(-0.2719, 0.3898, 0.72),
    vec3(-0.2480, 0.3858, 0.78), vec3(-0.2241, 0.3823, 0.84), vec3(-0.2001, 0.3793, 0.88),
    vec3(-0.1760, 0.3766, 0.92), vec3(-0.1519, 0.3744, 0.95), vec3(-0.1278, 0.3726, 0.98),
    vec3(-0.1037, 0.3711, 0.99), vec3(-0.0795, 0.3699, 1.00), vec3(-0.0553, 0.3691, 1.00),
    vec3(-0.0311, 0.3686, 1.00), vec3(-0.0069, 0.3684, 1.00), vec3(0.0173, 0.3689, 1.00),
    vec3(0.0413, 0.3712, 1.00), vec3(0.0651, 0.3756, 1.00), vec3(0.0884, 0.3822, 1.00),
    vec3(0.1108, 0.3914, 1.00), vec3(0.1315, 0.4038, 1.00), vec3(0.1492, 0.4202, 1.00),
    vec3(0.1607, 0.4413, 1.00), vec3(0.1611, 0.4653, 0.96), vec3(0.1504, 0.4867, 0.64),
    vec3(0.1330, 0.5035, 0.18), vec3(0.1124, 0.5161, -0.30), vec3(0.0902, 0.5256, -0.73),
    vec3(0.0670, 0.5324, -1.08), vec3(0.0432, 0.5369, -1.32), vec3(0.0191, 0.5394, -1.47),
    vec3(-0.0050, 0.5400, -1.50), vec3(-0.0292, 0.5386, -1.50), vec3(-0.0532, 0.5353, -1.50),
    vec3(-0.0767, 0.5298, -1.50), vec3(-0.0996, 0.5220, -1.50), vec3(-0.1213, 0.5113, -1.50),
    vec3(-0.1408, 0.4970, -1.50), vec3(-0.1560, 0.4783, -1.50), vec3(-0.1625, 0.4553, -1.50),
    vec3(-0.1569, 0.4320, -1.50), vec3(-0.1423, 0.4128, -1.50), vec3(-0.1244, 0.3966, -1.50),
    vec3(-0.1061, 0.3807, -1.50), vec3(-0.0876, 0.3652, -1.51), vec3(-0.0688, 0.3499, -1.52),
    vec3(-0.0499, 0.3348, -1.53), vec3(-0.0307, 0.3201, -1.54), vec3(-0.0114, 0.3055, -1.55),
    vec3(0.0082, 0.2913, -1.56), vec3(0.0279, 0.2772, -1.57), vec3(0.0478, 0.2635, -1.59),
    vec3(0.0679, 0.2500, -1.60), vec3(0.0881, 0.2367, -1.61), vec3(0.1086, 0.2238, -1.63),
    vec3(0.1292, 0.2111, -1.65), vec3(0.1500, 0.1988, -1.66), vec3(0.1711, 0.1868, -1.68),
    vec3(0.1923, 0.1752, -1.70), vec3(0.2137, 0.1640, -1.71), vec3(0.2353, 0.1531, -1.73),
    vec3(0.2572, 0.1428, -1.75), vec3(0.2793, 0.1330, -1.77), vec3(0.3017, 0.1237, -1.79),
    vec3(0.3243, 0.1151, -1.80), vec3(0.3472, 0.1072, -1.82), vec3(0.3703, 0.1001, -1.84),
    vec3(0.3937, 0.0940, -1.86), vec3(0.4174, 0.0890, -1.88), vec3(0.4413, 0.0852, -1.90),
    vec3(0.4654, 0.0831, -1.92), vec3(0.4896, 0.0827, -1.93), vec3(0.5137, 0.0845, -1.95),
    vec3(0.5375, 0.0889, -1.96), vec3(0.5605, 0.0961, -1.98), vec3(0.5824, 0.1066, -1.99),
    vec3(0.6023, 0.1202, -2.00), vec3(0.6200, 0.1366, -2.00), vec3(0.6351, 0.1555, -2.00),
    vec3(0.6480, 0.1760, -2.00), vec3(0.6586, 0.1977, -2.00), vec3(0.6668, 0.2205, -2.00),
    vec3(0.6724, 0.2440, -2.00), vec3(0.6755, 0.2680, -2.00), vec3(0.6762, 0.2922, -2.00),
    vec3(0.6746, 0.3163, -1.99), vec3(0.6711, 0.3403, -1.99), vec3(0.6657, 0.3638, -1.98),
    vec3(0.6588, 0.3870, -1.98), vec3(0.6504, 0.4097, -1.97), vec3(0.6407, 0.4319, -1.97),
    vec3(0.6298, 0.4535, -1.96), vec3(0.6180, 0.4746, -1.95), vec3(0.6052, 0.4952, -1.94),
    vec3(0.5916, 0.5151, -1.92), vec3(0.5771, 0.5346, -1.91), vec3(0.5620, 0.5534, -1.90),
    vec3(0.5461, 0.5717, -1.88), vec3(0.5296, 0.5894, -1.86), vec3(0.5125, 0.6065, -1.84),
    vec3(0.4948, 0.6230, -1.82), vec3(0.4765, 0.6388, -1.80), vec3(0.4576, 0.6540, -1.77),
    vec3(0.4382, 0.6684, -1.74), vec3(0.4182, 0.6821, -1.71), vec3(0.3977, 0.6949, -1.68),
    vec3(0.3766, 0.7067, -1.64), vec3(0.3550, 0.7175, -1.60), vec3(0.3328, 0.7272, -1.55),
    vec3(0.3100, 0.7354, -1.50), vec3(0.2867, 0.7420, -1.44), vec3(0.2630, 0.7466, -1.38),
    vec3(0.2389, 0.7489, -1.31), vec3(0.2148, 0.7483, -1.23), vec3(0.1909, 0.7443, -1.14),
    vec3(0.1681, 0.7363, -1.04), vec3(0.1471, 0.7243, -0.93), vec3(0.1288, 0.7085, -0.82),
    vec3(0.1136, 0.6897, -0.70), vec3(0.1015, 0.6688, -0.59), vec3(0.0922, 0.6465, -0.48),
    vec3(0.0853, 0.6233, -0.38), vec3(0.0805, 0.5996, -0.28), vec3(0.0773, 0.5756, -0.18),
    vec3(0.0756, 0.5515, -0.09), vec3(0.0750, 0.5273, 0.00)
);

// The metal: its reflectance head on (a dark steel), and its roughness.
const vec3  METAL_F0        = vec3(0.42, 0.42, 0.45);
const float METAL_ROUGHNESS = 0.5;
// A clear coat over it: its roughness, and how much of the light it reflects head on.
const float COAT_ROUGHNESS = 0.12;
const float COAT_F0        = 0.06;
// Its bevel, a quarter round worked out from its shape (u_shape): how far in from its edge it runs
// and how high it rounds up over that, in the logo's heights; and how far apart its slope is read,
// in texels.
const float BEVEL_WIDTH  = 0.012;
const float BEVEL_HEIGHT = 0.01;
const float BEVEL_READ   = 1.5;

// The comet as a light: its color and power; how high above the logo's face it passes, and how
// far its light reaches before it has none, in screen heights - near, so it shows only a little of
// the logo at a time.
const vec3  LIGHT_TINT   = vec3(1.0, 0.97, 0.94);
const float LIGHT_POWER  = 0.35;
const float LIGHT_HEIGHT = 0.035;
const float LIGHT_RADIUS = 0.125;
// And a faint light it casts over the whole screen, so wherever it is it can be seen moving: how
// bright, as a soft glow on the metal - brighter on the side towards it - and the distance it
// halves over, in screen heights.
const float LIGHT_FILL        = 0.045;
const float LIGHT_FILL_RADIUS = 0.3;

// Behind the logo, its light catches the faint haze about the logo's edges and streams away from
// them, the logo's shadow cut into the rays; the comet itself is never seen. The samples each ray
// is traced with back towards it, each ray's set off by a pixel's own fraction of a step so they
// band into no lines; the distance over which a ray's light fades, in screen heights; how far past
// the logo's edge the haze reaches, in its heights; the distance the light's reach halves over, in
// screen heights; and how bright. They are the light's color.
const int   RAY_STEPS    = 64;
const float RAY_FADE     = 0.15;
const float RAY_HAZE     = 0.12;
const float RAY_FALLOFF  = 0.18;
const float RAY_GAIN     = 0.22;

// The strike: its flash, as a radius in screen heights and a brightness, and how fast it dies, a
// second; the ring of light it sends out, its speed and width in screen heights and how bright.
const float FLASH_RADIUS = 0.014;
const float FLASH_GAIN   = 4.0;
const float FLASH_DECAY  = 10.0;
const float RING_SPEED   = 0.45;
const float RING_WIDTH   = 0.012;
const float RING_GAIN    = 0.8;
// The flash's bloom about it: its radius, in screen heights, and brightness.
const float BLOOM_RADIUS = 0.06;
const float BLOOM_GAIN   = 0.6;
// The heat: how far it has spread from the strike at first and at last, in screen heights; how
// bright the hottest glows, linear; its halo, and how far past the edge its tight part falls off, in
// the logo's heights; the shimmer's reach, in shares of the logo, and the scale and speed of the
// turbulence it and the glow move with.
const float HEAT_REACH_FROM = 0.045;
const float HEAT_REACH      = 0.55;
const float HEAT_GAIN       = 1.8;
const float HEAT_HALO       = 0.3;
const float HALO_NEAR       = 0.012;
const float HEAT_SHIMMER    = 0.004;
const float HEAT_SCALE      = 7.0;
const float HEAT_RISE       = 0.9;
// The scale that forms on it as it heats: its cells, a logo's height across, and how dark their
// cracks draw across the glow. How much more the air above it shimmers than over it.
const float SCALE_CELLS  = 55.0;
const float SCALE_DARK   = 0.25;
const float HAZE_ABOVE   = 3.0;
// The light the hot logo gives off into the air: how bright, and how fast its far glow falls off
// about its middle, in the logo's heights.
const float EMISSION     = 0.5;
const float EMISSION_FAR = 5.0;
// The sparks the strike throws: how many; their speeds, in screen heights a second, and the lift
// their burst has; the pull down on them; their longest life, in seconds; how long a streak each
// draws, in seconds of its flight; and their width, in screen heights, at most.
const int   SPARKS        = 64;
const vec2  SPARK_SPEED   = vec2(0.15, 1.0);
const float SPARK_LIFT    = 0.2;
const float SPARK_GRAVITY = 0.9;
const float SPARK_LIFE    = 1.8;
const float SPARK_STREAK  = 0.05;
const float SPARK_WIDTH   = 0.0012;
// The embers the hot logo gives off: how many; the seconds each lives, at least and at most; how
// fast they rise, in screen heights a second; how far they sway; and their size, at most.
const int   EMBERS     = 80;
const vec2  EMBER_LIFE = vec2(1.4, 2.8);
const vec2  EMBER_RISE = vec2(0.04, 0.13);
const float EMBER_SWAY = 0.015;
const float EMBER_SIZE = 0.0018;
// Film grain, in 8-bit steps either way: it also keeps a dark gradient from banding.
const float GRAIN = 1.5;
// The mip the logo's coverage is read blurred at: how deep inside it a point is, or how near it.
const float DEPTH_LOD = 4.5;

// @p image at @p uv, drawn far smaller than it is: four finer reads about the pixel, so its fine
// lines stay sharp without shimmering.
vec4 sharpRead(sampler2D image, vec2 uv) {
    const vec2 pixel = 0.25 * fwidth(uv);
    vec4 sum = texture(image, uv + vec2(-pixel.x, -pixel.y), SHARPEN);
    sum     += texture(image, uv + vec2(pixel.x, -pixel.y), SHARPEN);
    sum     += texture(image, uv + vec2(-pixel.x, pixel.y), SHARPEN);
    sum     += texture(image, uv + vec2(pixel.x, pixel.y), SHARPEN);
    return 0.25 * sum;
}

// x squared: pow() is undefined for a negative base.
float square(float x) {
    return x * x;
}

// The screen's width in its heights.
float screenAspect() {
    return u_rect.w * u_logoAspect / u_rect.z;
}

// A point of the logo image, @p uv, in screen heights from the screen's bottom left.
vec2 onScreen(vec2 uv) {
    return (u_rect.xy + uv * u_rect.zw) * vec2(screenAspect(), 1.0);
}

// The comet @p t of the way along its path: xy in screen heights from the bottom left, z its height
// over the logo's face in screen heights, below zero behind it.
vec3 cometAt(float t) {
    const float at = clamp(t, 0.0, 1.0) * float(PATH_POINTS - 1);
    const int   i  = min(int(at), PATH_POINTS - 2);
    const vec3  p  = mix(PATH[i], PATH[i + 1], at - float(i));
    return vec3(p.x + 0.5 * screenAspect(), p.y, p.z * LIGHT_HEIGHT);
}

// The GGX microfacet reflection of roughness @p rough, Smith-shadowed, for the light direction
// @p l and the half vector @p h about the normal @p n, the eye straight above; Fresnel not applied.
float ggx(vec3 n, vec3 l, vec3 h, float rough) {
    const float nl   = max(dot(n, l), 0.0);
    const float nv   = max(n.z, 1e-3);
    const float nh   = max(dot(n, h), 0.0);
    const float a2   = rough * rough * rough * rough;
    const float lobe = nh * nh * (a2 - 1.0) + 1.0;
    const float d    = a2 / (PI * lobe * lobe);
    const float k    = (rough + 1.0) * (rough + 1.0) / 8.0;
    const float g    = nv / (nv * (1.0 - k) + k) * nl / (nl * (1.0 - k) + k);
    return d * g / max(4.0 * nv * nl, 1e-3) * nl;
}

// The light a point light at @p toLight (from the surface, its length the distance) reflects to
// the eye straight above, off metal of normal @p n under a clear coat: each a GGX lobe with
// Schlick's Fresnel - the satin metal broad, so the light shows across the face from afar; the
// coat sharp, its highlight.
vec3 metalLit(vec3 n, vec3 toLight) {
    const vec3  l       = normalize(toLight);
    const vec3  h       = normalize(l + vec3(0.0, 0.0, 1.0));
    const float facing  = pow(1.0 - max(h.z, 0.0), 5.0);
    const vec3  metal   = (METAL_F0 + (1.0 - METAL_F0) * facing) * ggx(n, l, h, METAL_ROUGHNESS);
    const float coat    = (COAT_F0 + (1.0 - COAT_F0) * facing) * ggx(n, l, h, COAT_ROUGHNESS);
    // Never nearer than a light height: one passing through the face is not a point of fire. And
    // falling all the way from it to nothing at its reach.
    const float near    = max(dot(toLight, toLight), LIGHT_HEIGHT * LIGHT_HEIGHT);
    const float reach   = clamp(1.0 - near / (LIGHT_RADIUS * LIGHT_RADIUS), 0.0, 1.0);
    const float far     = 1.0 / (1.0 + near / (LIGHT_FILL_RADIUS * LIGHT_FILL_RADIUS));
    const vec3  power   = LIGHT_TINT * (LIGHT_POWER * reach * reach / near);
    const float soft    = LIGHT_FILL * far * (0.35 + 0.65 * max(dot(n, l), 0.0));
    const vec3  fill    = LIGHT_TINT * METAL_F0 * soft;
    return (metal * (1.0 - coat) + coat) * power + fill;
}

// How far inside the logo @p uv is, in its heights, below zero outside it: its shape, carried on
// past the image's edge so nothing read from it stops there.
float shapeAt(vec2 uv) {
    const float past = length(max(abs(uv - 0.5) - 0.5, 0.0) * vec2(u_logoAspect, 1.0));
    return texture(u_shape, clamp(uv, 0.0, 1.0)).r - past;
}

// The logo's coverage about @p uv blurred, near (x) and wide (y): what the hot logo gives off,
// read past the image's edge as far as the blur spreads.
vec2 glowAt(vec2 uv) {
    return texture(u_glow, uv * u_glowRect.xy + u_glowRect.zw).rg;
}

// The logo's normal at @p uv: up from its flat face, rolling over its rounded bevel to its edge.
vec3 normalAt(vec2 uv) {
    const vec2  texel = BEVEL_READ / vec2(textureSize(u_shape, 0));
    const vec2  ex    = vec2(texel.x, 0.0);
    const vec2  ey    = vec2(0.0, texel.y);
    const float sx    = texture(u_shape, uv + ex).r - texture(u_shape, uv - ex).r;
    const float sy    = texture(u_shape, uv + ey).r - texture(u_shape, uv - ey).r;
    // Which way inside is, in the logo's heights per its heights: across, the image is wider.
    const vec2  inward = vec2(sx / (2.0 * texel.x * u_logoAspect), sy / (2.0 * texel.y));
    // A quarter round: steepest at the edge, flat a bevel in.
    const float into  = clamp(texture(u_shape, uv).r / BEVEL_WIDTH, 0.0, 1.0);
    const float rest  = min(1.0 - into, 0.97);
    const float rise  = BEVEL_HEIGHT / BEVEL_WIDTH * rest / sqrt(1.0 - rest * rest);
    return normalize(vec3(-rise * inward, 1.0));
}

// A hash of @p n, 0..1.
float hash(float n) {
    return fract(sin(n * 127.1 + 311.7) * 43758.5453);
}

// How near @p p is to the segment from @p a to @p b (x), and how far along it, 0 at a, 1 at b (y).
vec2 toSegment(vec2 p, vec2 a, vec2 b) {
    const vec2  ab = b - a;
    const float t  = clamp(dot(p - a, ab) / max(dot(ab, ab), 1e-8), 0.0, 1.0);
    return vec2(length(p - a - ab * t), t);
}

// What a spark or an ember glows at @p left of its heat, 1 white-hot to 0 spent: yellow-white, then
// orange, then a dull red.
vec3 cooling(float left) {
    const vec3 ember = mix(vec3(0.8, 0.1, 0.02), vec3(1.0, 0.45, 0.08), smoothstep(0.0, 0.5, left));
    return mix(ember, vec3(1.0, 0.9, 0.7), smoothstep(0.55, 1.0, left));
}

// The sparks the strike at @p from throws, @p t seconds on, as seen at @p here: each flies out,
// falls, flickers and cools, drawn as the streak it moved along - a hot core in a soft glow.
vec3 sparksAt(vec2 here, vec2 from, float t) {
    vec3 seen = vec3(0.0);
    for (int i = 0; i < SPARKS; ++i) {
        const float n    = float(i);
        const float life = SPARK_LIFE * mix(0.3, 1.0, hash(n + 0.71));
        if (t > life) continue;
        // Flung outward, more up than down; each born a little out from the strike.
        const float turn = PI * mix(-0.25, 1.25, hash(n));
        const vec2  away = vec2(cos(turn), sin(turn));
        const float fast = mix(SPARK_SPEED.x, SPARK_SPEED.y, pow(hash(n + 0.37), 1.5));
        const vec2  v    = away * fast + vec2(0.0, SPARK_LIFT);
        const vec2  born = from + away * (0.004 + 0.008 * hash(n + 0.19));
        const float was  = max(t - SPARK_STREAK * mix(0.4, 1.6, hash(n + 0.53)), 0.0);
        const vec2  now  = born + v * t - vec2(0.0, 0.5 * SPARK_GRAVITY * t * t);
        const vec2  then = born + v * was - vec2(0.0, 0.5 * SPARK_GRAVITY * was * was);
        const vec2  near = toSegment(here, then, now);
        const float w    = SPARK_WIDTH * mix(0.45, 1.0, hash(n + 0.13));
        const float left = 1.0 - t / life;
        // Bright at its head, fading down its tail.
        const float tail = near.y * near.y;
        const float d2   = near.x * near.x;
        const float glow = exp(-d2 / (w * w)) * 2.2 + exp(-d2 / (12.0 * w * w)) * 0.2;
        const float beat = 0.7 + 0.3 * sin(t * 45.0 + n * 7.0);
        const float hot  = mix(0.5, 1.0, hash(n + 0.91));
        seen += cooling(left) * (left * beat * hot * tail * glow);
    }
    return seen;
}

// A smooth value noise, 0..1.
float noise(vec2 p) {
    const vec2 cell = floor(p);
    const vec2 f    = fract(p);
    const vec2 w    = f * f * (3.0 - 2.0 * f);
    const vec4 h    = fract(sin(vec4(
        dot(cell, vec2(127.1, 311.7)),
        dot(cell + vec2(1.0, 0.0), vec2(127.1, 311.7)),
        dot(cell + vec2(0.0, 1.0), vec2(127.1, 311.7)),
        dot(cell + vec2(1.0, 1.0), vec2(127.1, 311.7))
    )) * 43758.5453);
    return mix(mix(h.x, h.y, w.x), mix(h.z, h.w, w.x), w.y);
}

// What metal glows at @p t of the way to melting, as hot metal does: nothing, then a dull deep red,
// red, orange, and at its hottest a yellow-orange; brighter the hotter.
vec3 heatColor(float t) {
    vec3 tint = mix(vec3(0.5, 0.0, 0.0), vec3(1.0, 0.07, 0.0), smoothstep(0.1, 0.45, t));
    tint      = mix(tint, vec3(1.0, 0.32, 0.03), smoothstep(0.45, 0.75, t));
    tint      = mix(tint, vec3(1.0, 0.58, 0.18), smoothstep(0.75, 1.0, t));
    return tint * (smoothstep(0.05, 0.35, t) * (0.25 + 1.6 * t * t));
}

// Noise in octaves, 0..1: the uneven heat of metal, coarse and fine.
float fbm(vec2 p) {
    return 0.5 * noise(p) + 0.3 * noise(p * 2.1 + 3.7) + 0.2 * noise(p * 4.3 + 9.1);
}

// How far the heat has spread from where the logo was struck, in screen heights.
float heatReach() {
    return mix(HEAT_REACH_FROM, HEAT_REACH, smoothstep(0.0, 0.9, u_heat));
}

// How hot the logo is at @p uv, @p here on screen, 0..1: hottest where it was struck - white hot
// there at the last - and cooling with distance to a dull red at its far side; deep inside it more
// than at its thin edges, and a little uneven, the unevenness slowly rising.
float temperatureAt(vec2 uv, vec2 here) {
    const float d      = distance(cometAt(1.0).xy, here);
    const float near   = pow(1.0 - smoothstep(0.0, heatReach(), d), 0.8);
    const float depth  = textureLod(u_logo, uv, DEPTH_LOD).a;
    const float uneven = fbm(uv * HEAT_SCALE * 2.0 + vec2(0.0, -u_time * HEAT_RISE * 0.35));
    // Struck, the spot is hot at once.
    return (0.4 + 0.6 * u_heat) * near * mix(0.92, 1.0, depth) * mix(0.85, 1.1, uneven);
}

// How near @p p is to the border between the two nearest of a jittered grid of points: the cracks
// between cells, as the scale on hot steel breaks.
float cracks(vec2 p) {
    const vec2 cell   = floor(p);
    float      first  = 8.0;
    float      second = 8.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const vec2  at    = cell + vec2(float(x), float(y));
            const float seed  = dot(at, vec2(1.0, 57.0));
            const float d     = distance(p, at + vec2(hash(seed), hash(seed + 0.5)));
            second = d < first ? first : min(second, d);
            first  = min(first, d);
        }
    }
    return second - first;
}

// The embers rising off the hot logo, @p since seconds after it was struck, as seen at @p here: each
// born at a hot spot of the metal, rising faster as it goes, swaying, cooling and dying away - a
// core in a soft halo, drawn along the short streak it rose.
vec3 embersAt(vec2 here, float since) {
    vec3 seen = vec3(0.0);
    for (int i = 0; i < EMBERS; ++i) {
        const float n    = float(i) + 100.0;
        const float life = mix(EMBER_LIFE.x, EMBER_LIFE.y, hash(n));
        const float late = hash(n + 0.5) * life;
        if (since < late) continue;
        const float age  = mod(since - late, life);
        const float born = floor((since - late) / life);
        // Born on the metal, and only where it is hot enough to give one off.
        const vec2  uv   = vec2(hash(n + born * 7.3), hash(n + born * 3.1 + 0.2));
        const vec2  at   = onScreen(uv);
        const float heat = textureLod(u_logo, uv, 3.0).a * temperatureAt(uv, at);
        if (heat < 0.3) continue;
        const float rise = mix(EMBER_RISE.x, EMBER_RISE.y, hash(n + 0.9));
        const float sway = 6.0 * hash(n + 0.3);
        const float ago  = max(age - 0.08, 0.0);
        const vec2  p    = at + vec2(EMBER_SWAY * sin(age * 2.3 + sway), rise * age * (1.0 + 0.6 * age));
        const vec2  q    = at + vec2(EMBER_SWAY * sin(ago * 2.3 + sway), rise * ago * (1.0 + 0.6 * ago));
        const vec2  near = toSegment(here, q, p);
        const float size = EMBER_SIZE * mix(0.35, 1.0, hash(n + 0.6)) * (1.0 - 0.5 * age / life);
        const float left = 1.0 - age / life;
        const float beat = 0.7 + 0.3 * sin(age * 17.0 + n);
        const float d2   = near.x * near.x;
        const float glow = (exp(-d2 / (size * size)) + 0.25 * exp(-d2 / (9.0 * size * size))) * near.y;
        seen += cooling(left * heat) * (smoothstep(0.0, 0.15, age) * left * beat * glow);
    }
    return seen;
}

void main() {
    vec3  shown = vec3(0.0);
    float cover = 0.0;  // the logo's, where its face hides the light it gives off

    const vec2 here   = vUV * vec2(screenAspect(), 1.0);  // in screen heights
    const bool flying = u_comet >= 0.0 && u_comet <= 1.0;
    const bool struck = u_struck >= 0.0;

    // No flip: the loader asks stb for bottom-up rows, the order the triangle's v runs in. Nothing
    // decoded is not sampled: that reads whatever the unit holds, often a glyph atlas.
    vec2 uv = (vUV - u_rect.xy) / u_rect.zw;
    // Hot, it shimmers: the logo is read through rising, wavering air.
    if (u_ident != 0 && u_heat > 0.0) {
        const vec2 drift = vec2(0.0, -u_time * HEAT_RISE);
        const vec2 waver = vec2(noise(uv * HEAT_SCALE + drift), noise(uv * HEAT_SCALE + drift + 17.0));
        // The hot air rises off it: more above it than over it.
        const float above = 1.0 + HAZE_ABOVE * smoothstep(0.7, 1.15, uv.y);
        uv += (waver - 0.5) * (HEAT_SHIMMER * u_heat * above);
    }
    if (u_hasLogo != 0 && uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0) {
        const vec4 texel = sharpRead(u_logo, uv);
        if (u_ident == 0) {
            // An image that is not an ident is shown as it is.
            shown += texel.rgb * texel.a;
        } else {
            // Its edge from its shape: crisp at any size.
            const float into  = texture(u_shape, uv).r;
            const float solid = clamp(into / max(fwidth(into), 1e-6) + 0.5, 0.0, 1.0);
            vec3 lit  = vec3(0.0);
            vec3 halo = vec3(0.0);  // what the strike gives off just past its edges

            // How deep inside the logo a point is, for where the heat is fiercest.
            const float depth = textureLod(u_logo, uv, DEPTH_LOD).a;

            // The comet's light on the metal: its face while it is in front, the bevelled edges that
            // face it as it slips behind, and none of it once it is well behind.
            if (flying) {
                const vec3 comet = cometAt(u_comet);
                lit += metalLit(normalAt(uv), vec3(comet.xy - here, comet.z));
            }

            if (struck) {
                // The strike: a white-hot point on the M blooming and dying fast, and a wave of heat
                // racing out through the metal.
                const float d     = distance(cometAt(1.0).xy, here);
                const float flash = FLASH_GAIN * exp(-u_struck * FLASH_DECAY)
                    * exp(-d * d / (FLASH_RADIUS * FLASH_RADIUS));
                const float bloom = BLOOM_GAIN * exp(-u_struck * 6.0)
                    * exp(-d * d / (BLOOM_RADIUS * BLOOM_RADIUS));
                const float ring  = RING_GAIN * exp(-u_struck * 2.5)
                    * exp(-square((d - u_struck * RING_SPEED) / RING_WIDTH));
                lit  += vec3(1.0, 0.92, 0.8) * flash + vec3(1.0, 0.6, 0.3) * bloom + heatColor(1.0) * ring;
                halo += vec3(1.0, 0.5, 0.25) * (0.5 * flash + bloom) + heatColor(0.9) * (0.5 * ring);

                // Red hot from there, spreading through it and hotter by the moment. Its thin bevels
                // lose their heat first, so its form shows; scale forms on it as it heats, cracking
                // dark across the glow; and where the heat has reached, a brighter front.
                const float t     = temperatureAt(uv, here);
                const float face  = mix(0.85, 1.0, smoothstep(0.8, 0.98, normalAt(uv).z));
                const vec2  cells = uv * vec2(u_logoAspect, 1.0) * SCALE_CELLS;
                const float crack = 1.0 - smoothstep(0.0, 0.05, cracks(cells));
                const float scale = 1.0 - SCALE_DARK * crack * smoothstep(0.25, 0.6, t);
                const float front = exp(-square((d - 0.9 * heatReach()) / 0.02)) * (1.0 - u_heat);
                lit  += heatColor(t * face) * (HEAT_GAIN * scale) + heatColor(0.85) * (0.5 * front);
            }
            // The image's own margin bounds what it gives off: faded out before its edge.
            const vec2  margin = min(uv, 1.0 - uv);
            const float inside = smoothstep(0.0, 0.05, min(margin.x, margin.y));
            shown += solid * lit + (1.0 - solid) * halo * depth * inside;
            cover  = solid;
        }
    }

    // God rays: the comet behind the logo lights the haze about its edges, and each ray is traced
    // back towards it, dark where the logo stands between.
    if (u_ident != 0 && u_hasLogo != 0 && flying) {
        const vec3  comet  = cometAt(u_comet);
        // Fading out as it comes in front, while its light comes onto the metal, not before.
        const float behind = smoothstep(-0.6, 1.0, -comet.z / LIGHT_HEIGHT);
        if (behind > 0.0) {
            const vec2  toward = comet.xy - here;
            const vec2  step   = toward / float(RAY_STEPS);
            const float pace   = length(step);
            // Each pixel's own start within a step.
            const float start  = interleavedGradientNoise(gl_FragCoord.xy);
            float sum = 0.0;
            for (int i = 0; i < RAY_STEPS; ++i) {
                const float along = float(i) + start;
                const vec2  at    = here + step * along;
                const float into  = shapeAt((at / vec2(screenAspect(), 1.0) - u_rect.xy) / u_rect.zw);
                const float shut  = smoothstep(-0.002, 0.002, into);
                const float haze  = exp(min(into, 0.0) / RAY_HAZE);
                const vec2  off   = at - comet.xy;
                const float lit   = 1.0 / (1.0 + dot(off, off) / (RAY_FALLOFF * RAY_FALLOFF));
                sum += (1.0 - shut) * haze * lit * exp(-pace * along / RAY_FADE) * pace;
            }
            shown += LIGHT_TINT * (RAY_GAIN / RAY_FADE * behind * sum * (1.0 - cover));
        }
    }

    // The hot logo lights the air about it, behind its own face: a halo as hot as the metal it is
    // near - tight along its edge, from its shape, and wider, its coverage blurred; the light it
    // sheds, near, its coverage blurred further; and, far, a wide falloff about its middle.
    if (u_ident != 0 && u_hasLogo != 0 && u_heat > 0.0) {
        const float edge  = exp(-max(-shapeAt(uv), 0.0) / HALO_NEAR);
        const vec2  glow  = glowAt(uv);
        const float halo  = 0.35 * edge + glow.x;
        const vec2  span  = vec2(u_logoAspect, 1.0);
        const float near  = glow.y;
        const float far   = 0.25 * exp(-max(length((uv - 0.5) * span) - 0.3, 0.0) * EMISSION_FAR);
        const vec3  given = heatColor(0.85 * temperatureAt(uv, here)) * (HEAT_HALO * halo)
            + heatColor(0.8 * u_heat) * (EMISSION * (0.6 * near + 0.4 * far));
        shown += given * (1.0 - cover);
    }

    // The strike's sparks, and the embers the heat gives off.
    if (u_ident != 0 && u_hasLogo != 0 && struck) {
        shown += 2.5 * sparksAt(here, cometAt(1.0).xy, u_struck);
        shown += 1.8 * embersAt(here, u_struck);
    }

    const vec2 line = (vUV - u_captionRect.xy) / u_captionRect.zw;
    if (u_hasCaption != 0 && line.x >= 0.0 && line.x <= 1.0 && line.y >= 0.0 && line.y <= 1.0) {
        const vec4 texel = sharpRead(u_caption, line);
        shown += texel.rgb * texel.a * CAPTION_TONE * u_captionOpacity;
    }

    // Too bright for the glass, it is dimmed whole rather than clipped a channel at a time, which
    // would turn a bright red yellow.
    shown /= max(1.0, max(shown.r, max(shown.g, shown.b)));

    // Sampled linear from an sRGB texture, so encoded for the 8-bit backbuffer. The fade scales
    // the encoded value, so a linear ramp is linear on the glass. An ident has grain over it, as
    // film has; any other image is shown as it is.
    const float grain = fract(sin(dot(gl_FragCoord.xy + u_time * 61.0, vec2(12.9898, 78.233))) * 43758.5453);
    const float film  = u_ident != 0 ? (grain - 0.5) * (2.0 * GRAIN / 255.0) : 0.0;
    FragColor = vec4((linearToSrgb(shown) + film) * u_opacity, 1.0);
}
