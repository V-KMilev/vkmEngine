/**
 * Composite fragment shader - exposure -> AgX -> sRGB.
 *
 * The single owner of the display transform. Samples the resolved linear HDR
 * scene, applies camera exposure, the AgX tone mapping curve (Troy Sobotka /
 * minimal fit, "punchy" look), then the sRGB OETF for the 8-bit backbuffer.
 *
 * Exposure rides in the camera UBO (cameraPosition.w) bound for the frame.
 */
#version 420 core

in vec2 vUV;

out vec4 FragColor;

uniform sampler2D u_hdr;
uniform sampler2D u_bloom;
uniform float u_bloomStrength;

uniform sampler2D u_adaptedLum;
uniform int   u_autoExposure;
uniform float u_exposureKey;
uniform float u_exposureMin;
uniform float u_exposureMax;

// Color grading: 16^3 LUT laid out as a 256x16 strip, applied post-display.
uniform sampler2D u_colorLut;
uniform int   u_lutEnabled;
uniform float u_lutIntensity;

layout(std140, binding = 2) uniform CameraBlock {
    mat4 viewProjection;
    vec4 cameraPosition;  // xyz = position, w = exposure
    vec4 ambient;
} u_camera;

// 6th-order sigmoid fit of the AgX contrast curve.
vec3 agxContrast(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return  15.5     * x4 * x2
          - 40.14    * x4 * x
          + 31.96    * x4
          - 6.868    * x2 * x
          + 0.4298   * x2
          + 0.1191   * x
          - 0.00232;
}

vec3 agx(vec3 val) {
    const mat3 agxMat = mat3(
        0.842479062253094,  0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772,  0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const float minEv = -12.47393;
    const float maxEv =  4.026069;

    val = agxMat * val;
    val = clamp(log2(val), minEv, maxEv);
    val = (val - minEv) / (maxEv - minEv);
    return agxContrast(val);
}

vec3 agxEotf(vec3 val) {
    const mat3 agxMatInv = mat3(
         1.19687900512017,   -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368,  1.15190312990417,   -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433,  1.15107367264116);
    val = agxMatInv * val;
    // Back to display-linear; the sRGB OETF below re-encodes for the buffer.
    return pow(max(val, vec3(0.0)), vec3(2.2));
}

// "Punchy" look: mild saturation and contrast lift before the EOTF.
vec3 agxLook(vec3 val) {
    const vec3 luma = vec3(0.2126, 0.7152, 0.0722);
    float l = dot(val, luma);
    val = pow(val, vec3(1.05));
    return l + 1.05 * (val - l);
}

// 16^3 LUT as a 256x16 strip (16 blue slices of 16x16), bilinear in blue.
vec3 lutLookup(vec3 c) {
    const float N = 16.0;
    c = clamp(c, 0.0, 1.0);
    float blue = c.b * (N - 1.0);
    float b0 = floor(blue);
    float b1 = min(b0 + 1.0, N - 1.0);
    float fb = blue - b0;
    float v  = (c.g * (N - 1.0) + 0.5) / N;
    float u0 = (b0 * N + c.r * (N - 1.0) + 0.5) / (N * N);
    float u1 = (b1 * N + c.r * (N - 1.0) + 0.5) / (N * N);
    return mix(texture(u_colorLut, vec2(u0, v)).rgb,
               texture(u_colorLut, vec2(u1, v)).rgb, fb);
}

vec3 sRGBEncode(vec3 c) {
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(hi, lo, vec3(lessThanEqual(c, vec3(0.0031308))));
}

void main() {
    vec3 hdr = texture(u_hdr, vUV).rgb;

    // Energy-conserving bloom blend (linear HDR, before the display transform).
    vec3 bloom = textureLod(u_bloom, vUV, 0.0).rgb;
    hdr = mix(hdr, bloom, u_bloomStrength);

    // Manual camera exposure always applies; when auto-exposure is on it is
    // multiplied by key / adapted-luminance (so the camera value is EV bias).
    float exposure = max(u_camera.cameraPosition.w, 0.0);
    if (u_autoExposure == 1) {
        float adaptedLum = texture(u_adaptedLum, vec2(0.5)).r;
        float autoExp = u_exposureKey / max(adaptedLum, 1e-4);
        exposure *= clamp(autoExp, u_exposureMin, u_exposureMax);
    }
    hdr *= exposure;

    vec3 color = agx(hdr);
    color = agxLook(color);
    color = agxEotf(color);
    color = sRGBEncode(clamp(color, 0.0, 1.0));

    if (u_lutEnabled == 1) {
        color = mix(color, lutLookup(color), u_lutIntensity);
    }

    FragColor = vec4(color, 1.0);
}
