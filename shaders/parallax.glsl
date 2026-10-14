/*
 * Parallax occlusion mapping from the material's height map: the uv a view ray meets the height
 * field at, and the field's shadow on itself. Needs surface.glsl's u_heightTexture.
 */

// Depth below the top of the height field at uv, 0..1 (height maps are white
// where high). Explicit derivatives: the marches leave their loops per pixel,
// where implicit ones are undefined.
float heightDepth(vec2 uv, vec2 uvDx, vec2 uvDy) {
    return 1.0 - textureGrad(u_heightTexture, uv, uvDx, uvDy).r;
}

// March the height field along the tangent-space view direction and
// interpolate the crossing. More layers at grazing angles.
vec2 parallax(vec2 uv, vec3 viewDirTS, vec2 uvDx, vec2 uvDy) {
    const float MIN_LAYERS = 8.0;
    const float MAX_LAYERS = 32.0;
    float numLayers = mix(MAX_LAYERS, MIN_LAYERS, clamp(abs(viewDirTS.z), 0.0, 1.0));

    float layerDepth = 1.0 / numLayers;
    float curLayerDepth = 0.0;

    vec2 maxOffset = (viewDirTS.xy / max(viewDirTS.z, 0.001)) * u_material.heightScale;
    vec2 deltaUV = maxOffset / numLayers;

    vec2  curUV = uv;
    float curH  = heightDepth(curUV, uvDx, uvDy);

    for (int i = 0; i < int(MAX_LAYERS); ++i) {
        if (curLayerDepth >= curH) break;
        curUV -= deltaUV;
        curH = heightDepth(curUV, uvDx, uvDy);
        curLayerDepth += layerDepth;
    }

    vec2  prevUV = curUV + deltaUV;
    float afterD  = curH - curLayerDepth;
    float beforeD = heightDepth(prevUV, uvDx, uvDy) - curLayerDepth + layerDepth;
    float w       = afterD / (afterD - beforeD);
    return mix(curUV, prevUV, clamp(w, 0.0, 1.0));
}

// The height field's shadow on itself from the displaced uv: march toward the light, keeping the
// worst blocker above the ray, as any point above it casts a hard shadow. 1 = lit, 0 = shadowed.
float parallaxShadow(vec2 uv, vec3 lightDirTS, vec2 uvDx, vec2 uvDy) {
    if (lightDirTS.z <= 0.0) return 1.0;

    const float NUM_LAYERS = 16.0;

    float startDepth = heightDepth(uv, uvDx, uvDy);
    if (startDepth <= 0.0) return 1.0;

    // Each step rises deltaDepth and moves along the light's slope.
    float deltaDepth = startDepth / NUM_LAYERS;
    vec2  deltaUV    = (lightDirTS.xy / max(lightDirTS.z, 0.01)) * u_material.heightScale * deltaDepth;

    vec2  curUV    = uv      + deltaUV;
    float curDepth = startDepth - deltaDepth;

    float maxBlocker = 0.0;
    for (int i = 0; i < int(NUM_LAYERS); ++i) {
        if (curDepth <= 0.0) break;
        float d = heightDepth(curUV, uvDx, uvDy);
        if (d < curDepth) {
            maxBlocker = max(maxBlocker, curDepth - d);
        }
        curUV    += deltaUV;
        curDepth -= deltaDepth;
    }

    return clamp(1.0 - maxBlocker * 8.0, 0.0, 1.0);
}
