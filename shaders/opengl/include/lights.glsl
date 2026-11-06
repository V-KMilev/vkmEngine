/*
 * The GPU light SSBO and punctual falloff. Light is GpuLight (frame/light/gl_lights.h), each
 * component as its doc says; bindings, MAX_LIGHTS and LIGHT_* come from the shader prelude.
 */

struct Light {
    vec4 position;
    vec4 color;
    vec4 direction;
    vec4 spot;
    vec4 bake;
};

layout(std430, binding = SSBO_LIGHTS) readonly buffer LightsBlock {
    int   lightCount;
    int   _lp0; int _lp1; int _lp2;
    Light lights[MAX_LIGHTS];
} u_lights;

// The smooth window that takes a light to exactly zero at its radius.
float distanceWindow(float dist, float radius) {
    float window = clamp(1.0 - pow(dist / max(radius, 1e-3), 4.0), 0.0, 1.0);
    return window * window;
}

// Windowed inverse-square falloff: physically based with a finite range.
float distanceAttenuation(float dist, float radius) {
    return distanceWindow(dist, radius) / max(dist * dist, 1e-4);
}

// The type a light's shadow is read as: its own, but a spot too wide for one map casts through a
// cube as a point light does (bake.y, GpuLight).
int shadowTypeOf(Light light, int type) {
    return type == LIGHT_SPOT && light.bake.y > 0.5 ? LIGHT_POINT : type;
}

// 1 inside the inner angle, 0 past the outer, squared between. spot.xy are
// 1 / (cos inner - cos outer) and -cos outer times that: one multiply-add.
float spotAttenuation(Light light, vec3 L) {
    float cone = clamp(dot(light.direction.xyz, -L) * light.spot.x + light.spot.y, 0.0, 1.0);
    return cone * cone;
}

// A directional, point or spot light's falloff at p, and the unit L toward it.
float punctualAttenuation(Light light, int type, vec3 p, out vec3 L) {
    if (type == LIGHT_DIRECTIONAL) {
        L = -light.direction.xyz;
        return 1.0;
    }
    vec3  toLight = light.position.xyz - p;
    float dist    = length(toLight);
    L = toLight / max(dist, 1e-4);
    float atten = distanceAttenuation(dist, light.direction.w);
    if (type == LIGHT_SPOT) atten *= spotAttenuation(light, L);
    return atten;
}
