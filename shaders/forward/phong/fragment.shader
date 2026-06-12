#version 430 core

/*
 * Blinn-Phong over the engine's full light list - a debug stand-in for the PBR
 * forward shader. Covers every LightType: directional / point / spot punctual
 * lights, and Rect / Disk area lights (center-point diffuse + representative-
 * point specular, the cheap approximation - no LTC). Reads only albedo from the
 * material UBO; ignores the PBR maps and shadows. Writes linear HDR so the
 * composite pass still tonemaps it.
 */

#define MAX_LIGHTS 32  // must match Config::MAX_LIGHTS (engine_config.h / engine_config.glsl)

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT       1
#define LIGHT_SPOT        2
#define LIGHT_RECT        3
#define LIGHT_DISK        4

#define SHININESS    32.0
#define SPEC_STRENGTH 0.3

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

out vec4 FragColor;

// Only albedo (offset 0) is read; the rest of the material UBO is ignored.
layout(std140, binding = 0) uniform MaterialBlock {
    vec4 albedo;  // rgb + opacity
} u_material;

// Matches the GpuLight layout (gl_lights.h) - same as the forward shader.
struct Light {
    vec4 position;   // xyz = world position, w = type
    vec4 color;      // xyz = rgb,            w = intensity
    vec4 direction;  // xyz = world dir,      w = attenuation radius
    vec4 spot;       // x = inner cone, y = outer cone (radians)
    vec4 axisU;      // xyz = half-right world axis (Rect/Disk), w = twoSided
    vec4 axisV;      // xyz = half-up    world axis (Rect/Disk)
};

layout(std140, binding = 1) uniform LightsBlock {
    int   lightCount;
    int   _p0; int _p1; int _p2;
    Light lights[MAX_LIGHTS];
} u_lights;

layout(std140, binding = 2) uniform CameraBlock {
    mat4 viewProjection;
    vec4 cameraPosition;
} u_camera;

// Smooth windowed inverse-square falloff - matches the PBR forward shader.
float distanceAttenuation(float dist, float radius) {
    float invSqr = 1.0 / max(dist * dist, 1e-4);
    float window = clamp(1.0 - pow(dist / max(radius, 1e-3), 4.0), 0.0, 1.0);
    return invSqr * window * window;
}

// Point on a rect emitter closest to the reflection ray - representative-point
// specular. axisU / axisV are world-space half-extents.
vec3 areaRectClosestPoint(vec3 rayOrigin, vec3 rayDir, vec3 center, vec3 axisU, vec3 axisV) {
    vec3  n     = normalize(cross(axisU, axisV));
    float denom = dot(rayDir, n);
    float t     = (abs(denom) > 1e-4) ? dot(center - rayOrigin, n) / denom : -1.0;
    if (t <= 0.0) t = max(0.0, dot(center - rayOrigin, rayDir));
    vec3  hit   = rayOrigin + rayDir * t;

    vec3  d     = hit - center;
    float uLen  = length(axisU);
    float vLen  = length(axisV);
    vec3  uNorm = axisU / max(uLen, 1e-4);
    vec3  vNorm = axisV / max(vLen, 1e-4);
    return center + uNorm * clamp(dot(d, uNorm), -uLen, uLen)
                  + vNorm * clamp(dot(d, vNorm), -vLen, vLen);
}

// Same idea for a disk: project the ray hit into the disk plane, clamp to radius.
vec3 areaDiskClosestPoint(vec3 rayOrigin, vec3 rayDir, vec3 center, vec3 axisU, vec3 axisV) {
    vec3  n     = normalize(cross(axisU, axisV));
    float denom = dot(rayDir, n);
    float t     = (abs(denom) > 1e-4) ? dot(center - rayOrigin, n) / denom : -1.0;
    if (t <= 0.0) t = max(0.0, dot(center - rayOrigin, rayDir));
    vec3  hit   = rayOrigin + rayDir * t;

    vec3  d      = hit - center;
    d -= n * dot(d, n);
    float radius = length(axisU);
    float dLen   = length(d);
    if (dLen > radius) d *= (radius / dLen);
    return center + d;
}

// Blinn-Phong shading for one light direction L (already normalised), with a
// given specular direction Ls (the half-vector uses Ls; for punctual lights
// Ls == L, for area lights it points at the representative point).
vec3 shade(vec3 N, vec3 V, vec3 L, vec3 Ls, vec3 base, vec3 radiance) {
    float diff = max(dot(N, L), 0.0);
    vec3  H    = normalize(Ls + V);
    float spec = (diff > 0.0) ? pow(max(dot(N, H), 0.0), SHININESS) : 0.0;
    return radiance * (diff * base + spec * vec3(SPEC_STRENGTH));
}

void main() {
    vec3 N    = normalize(vNormal);
    vec3 V    = normalize(u_camera.cameraPosition.xyz - vWorldPos);
    vec3 base = u_material.albedo.rgb;

    vec3 color = 0.05 * base;  // flat ambient

    for (int i = 0; i < u_lights.lightCount && i < MAX_LIGHTS; ++i) {
        Light light     = u_lights.lights[i];
        int   type      = int(light.position.w);
        vec3  radiance0  = light.color.rgb * light.color.w;

        // Area lights: center for diffuse, closest-point on the emitter for the
        // specular highlight. Gated by which face of the emitter we are on.
        if (type == LIGHT_RECT || type == LIGHT_DISK) {
            vec3  U        = light.axisU.xyz;
            vec3  Vv       = light.axisV.xyz;
            bool  twoSided = light.axisU.w > 0.5;

            vec3  toCenter = light.position.xyz - vWorldPos;
            float dist     = length(toCenter);
            vec3  Lc       = toCenter / max(dist, 1e-4);

            // Emitter normal points along its emitting face; skip the back side
            // unless the light is two-sided.
            vec3  areaN = normalize(cross(U, Vv));
            float side  = dot(areaN, -Lc);
            if (side <= 0.0 && !twoSided) continue;

            float atten = distanceAttenuation(dist, light.direction.w);
            if (atten <= 0.0) continue;

            vec3 R  = reflect(-V, N);
            vec3 cp = (type == LIGHT_RECT)
                ? areaRectClosestPoint(vWorldPos, R, light.position.xyz, U, Vv)
                : areaDiskClosestPoint(vWorldPos, R, light.position.xyz, U, Vv);
            vec3 Ls = normalize(cp - vWorldPos);

            color += shade(N, V, Lc, Ls, base, radiance0 * atten);
            continue;
        }

        // Punctual lights: directional / point / spot.
        vec3  L;
        float atten = 1.0;
        if (type == LIGHT_DIRECTIONAL) {
            L = normalize(-light.direction.xyz);
        } else {
            vec3  toLight = light.position.xyz - vWorldPos;
            float dist    = length(toLight);
            L = toLight / max(dist, 1e-4);
            atten = distanceAttenuation(dist, light.direction.w);
            if (type == LIGHT_SPOT) {
                float cosInner = cos(light.spot.x);
                float cosOuter = cos(light.spot.y);
                float cd       = dot(normalize(light.direction.xyz), -L);
                float cone     = clamp((cd - cosOuter) / max(cosInner - cosOuter, 1e-4), 0.0, 1.0);
                atten *= cone * cone;
            }
        }
        if (atten <= 0.0) continue;

        color += shade(N, V, L, L, base, radiance0 * atten);
    }

    FragColor = vec4(color, 1.0);
}
