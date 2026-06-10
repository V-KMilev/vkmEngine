#version 430 core

in vec2 vUV;
out vec4 FragColor;

layout(binding = 0) uniform sampler2D u_hdr;  // linear HDR scene

// Tonemap + gamma a linear HDR sample to perceptual LDR. FXAA runs on this
// (edge detection wants perceptual luma, not linear radiance), so every tap
// goes through here.
vec3 resolve(vec2 uv) {
    vec3 c = texture(u_hdr, uv).rgb;
    c = c / (c + vec3(1.0));        // Reinhard tonemap
    return pow(c, vec3(1.0 / 2.2)); // gamma to the LDR backbuffer
}

float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// FXAA (Timothy Lottes, the widely-used simplified form): detect a luma edge
// from the 4 diagonal neighbours, find its direction, and blend a short span
// across it. Cheap first-pass AA until TAA/MSAA lands.
const float FXAA_SPAN_MAX   = 8.0;
const float FXAA_REDUCE_MUL = 1.0 / 8.0;
const float FXAA_REDUCE_MIN = 1.0 / 128.0;

void main() {
    vec2 texel = 1.0 / vec2(textureSize(u_hdr, 0));

    vec3 rgbM  = resolve(vUV);
    vec3 rgbNW = resolve(vUV + vec2(-1.0, -1.0) * texel);
    vec3 rgbNE = resolve(vUV + vec2( 1.0, -1.0) * texel);
    vec3 rgbSW = resolve(vUV + vec2(-1.0,  1.0) * texel);
    vec3 rgbSE = resolve(vUV + vec2( 1.0,  1.0) * texel);

    float lumaM  = luma(rgbM);
    float lumaNW = luma(rgbNW);
    float lumaNE = luma(rgbNE);
    float lumaSW = luma(rgbSW);
    float lumaSE = luma(rgbSE);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // Blend direction is perpendicular to the luma gradient.
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * FXAA_REDUCE_MUL,
                          FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-FXAA_SPAN_MAX), vec2(FXAA_SPAN_MAX)) * texel;

    vec3 rgbA = 0.5 * (resolve(vUV + dir * (1.0 / 3.0 - 0.5))
                     + resolve(vUV + dir * (2.0 / 3.0 - 0.5)));
    vec3 rgbB = rgbA * 0.5 + 0.25 * (resolve(vUV + dir * -0.5)
                                   + resolve(vUV + dir *  0.5));

    // The wider B span overshoots on thin features; fall back to A then.
    float lumaB = luma(rgbB);
    vec3 color = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;

    FragColor = vec4(color, 1.0);
}
