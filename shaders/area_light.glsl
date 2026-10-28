/*
 * Rect and disk lights: whether one reaches a point, its diffuse light there by the form factor
 * (form_factor.glsl), and the representative point the forward pass shades its specular from (Karis 2013).
 * A light's intensity is point-equivalent, so its radiance is intensity over its area.
 */

#include "lights.glsl"
#include "form_factor.glsl"

// Whether @p light reaches @p p: in front of its emitting face (U x V), or either side of a
// two-sided one, and within its range. @p facing is p's side, @p window the range's fade.
bool areaReaches(Light light, vec3 p, out float facing, out float window) {
    facing = dot(p - light.position.xyz, cross(light.axisU.xyz, light.axisV.xyz));
    window = 0.0;
    if (facing <= 0.0 && light.axisU.w <= 0.5) return false;
    window = distanceWindow(length(light.position.xyz - p), light.direction.w);
    return window > 0.0;
}

// The light's diffuse irradiance over PI at @p p around @p n, per unit of its color times
// intensity: radiance (one over its area) times the horizon-clipped form factor, faded by range.
float areaIrradiance(Light light, int type, vec3 p, vec3 n, float facing, float window) {
    float area;
    float formFactor = areaFormFactor(
        type, light.position.xyz, light.axisU.xyz, light.axisV.xyz, p, n, facing, area
    );
    return window * formFactor / max(area, 1e-4);
}

// The light's areaIrradiance at @p p around @p n where it reaches @p p (areaReaches), else 0.
float areaDiffuse(Light light, int type, vec3 p, vec3 n) {
    float facing, window;
    if (!areaReaches(light, p, facing, window)) return 0.0;
    return areaIrradiance(light, type, p, n, facing, window);
}

// Unit normal of an area emitter's plane; the clamp guards a zero-extent axis.
vec3 areaPlaneNormal(vec3 axisU, vec3 axisV) {
    vec3 n = cross(axisU, axisV);
    float nLen2 = max(dot(n, n), 1e-12);
    return n / sqrt(nLen2);
}

// Where a ray meets that plane; parallel or receding, the point on the ray
// nearest the centre instead of a hit behind it. The caller clamps onto the emitter.
vec3 areaPlaneHit(vec3 rayOrigin, vec3 rayDir, vec3 center, vec3 n) {
    float denom = dot(rayDir, n);
    float t = (abs(denom) > 1e-4)
        ? dot(center - rayOrigin, n) / denom
        : -1.0;
    if (t <= 0.0) {
        t = max(0.0, dot(center - rayOrigin, rayDir));
    }
    return rayOrigin + rayDir * t;
}

// The mirror ray's hit on the rect's plane, clamped onto it: the stand-in for its point nearest the
// ray (Karis 2013). axisU / axisV are half-extents.
vec3 areaRectClosestPoint(vec3 rayOrigin, vec3 rayDir, vec3 center, vec3 axisU, vec3 axisV) {
    vec3 hit = areaPlaneHit(rayOrigin, rayDir, center, areaPlaneNormal(axisU, axisV));

    vec3 d = hit - center;
    float uLen = length(axisU);
    float vLen = length(axisV);
    vec3 uNorm = axisU / max(uLen, 1e-4);
    vec3 vNorm = axisV / max(vLen, 1e-4);
    float uCoord = clamp(dot(d, uNorm), -uLen, uLen);
    float vCoord = clamp(dot(d, vNorm), -vLen, vLen);
    return center + uNorm * uCoord + vNorm * vCoord;
}

// The mirror ray's hit on the disk's plane, clamped onto it: the stand-in for its point nearest the
// ray. axisU / axisV both have its radius as length.
vec3 areaDiskClosestPoint(vec3 rayOrigin, vec3 rayDir, vec3 center, vec3 axisU, vec3 axisV) {
    vec3 n   = areaPlaneNormal(axisU, axisV);
    vec3 hit = areaPlaneHit(rayOrigin, rayDir, center, n);

    vec3 d = hit - center;
    d -= n * dot(d, n);
    float radius = length(axisU);
    float dLen   = length(d);
    if (dLen > radius) d *= (radius / dLen);
    return center + d;
}

// Karis (2013) Eq. 16: alpha' = saturate(alpha + r / (3*dist)), r the
// source's representative radius.
float areaBroadenedAlpha(float alpha, float sourceRadius, float dist) {
    return clamp(alpha + sourceRadius / max(3.0 * dist, 1e-4), 0.0, 1.0);
}
