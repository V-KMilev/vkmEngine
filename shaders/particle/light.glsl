/*
 * The light reaching a particle, for the vertex stages of the billboards and the strips: the
 * environment from all round, and its cluster's lights, each through its hard shadow at the
 * particle - a puff is lit at one point, which has no surface for the soft searches.
 *
 * Needs camera.glsl, lights.glsl (through area_light.glsl), shadows.glsl and ambient.glsl first.
 */

// The light at a billboard, as a ball takes it: ambient from all round, plus the lights that agree
// on a way and that way, which a sphere normal turns toward or away from.
struct ParticleLight {
    vec3 ambient;
    vec3 color;
    vec3 direction;
};

// The environment's mean radiance at p: its irradiance over PI averaged over the six axes, which
// is what an isotropic scatterer takes from all round.
vec3 meanAmbient(vec3 p) {
    vec3 sum = environmentIrradiance(p, vec3(1.0, 0.0, 0.0)) + environmentIrradiance(p, vec3(-1.0, 0.0, 0.0))
        + environmentIrradiance(p, vec3(0.0, 1.0, 0.0)) + environmentIrradiance(p, vec3(0.0, -1.0, 0.0))
        + environmentIrradiance(p, vec3(0.0, 0.0, 1.0)) + environmentIrradiance(p, vec3(0.0, 0.0, -1.0));
    return sum / 6.0;
}

// The light at p, gathered: the environment's mean from all round, the cluster's lights summed,
// and the way that sum comes from, each light weighted by its luminance.
void gatherParticleLight(vec3 p, out vec3 ambient, out vec3 sum, out vec3 toward) {
    ambient = meanAmbient(p);
    sum     = vec3(0.0);
    toward  = vec3(0.0);

    // Its cluster is where it is in the world, whichever projection draws it.
    float depth = max(viewDepthOf(p), u_camera.zNear);
    int   ci    = clusterIndex(screenUvOf(p), depth, u_camera.zNear, u_camera.zFar);

    uint count = u_clusters.clusters[ci].count;
    for (uint k = 0u; k < count; ++k) {
        Light light    = u_lights.lights[u_clusters.clusters[ci].indices[k]];
        vec3  lightCol = light.color.xyz * light.color.w;
        int   type     = int(light.position.w);

        vec3 radiance;
        vec3 L;
        if (type == LIGHT_RECT || type == LIGHT_DISK) {
            // An area light as the small source it is from far enough: its point-equivalent
            // intensity over the squared distance, by how squarely its face turns to the particle.
            float side, window;
            if (!areaReaches(light, p, side, window)) continue;
            vec3  toParticle = p - light.position.xyz;
            float dist       = length(toParticle);
            vec3  face       = areaPlaneNormal(light.axisU.xyz, light.axisV.xyz);
            float facing     = abs(dot(toParticle / max(dist, 1e-4), face));
            radiance = lightCol * window * facing / max(dist * dist, 1e-4) / PI;
            L        = -toParticle / max(dist, 1e-4);
        } else {
            float atten = punctualAttenuation(light, type, p, L);
            if (atten <= 0.0) continue;
            radiance = lightCol * atten * pointShadow(type, int(light.spot.w), p) / PI;
        }
        sum    += radiance;
        toward += L * luma(radiance);
    }
}

// As a ball takes it. Light that does not agree on a way - two lamps either side - is no way at
// all: it joins the ambient, at the half a ball's wrap takes on average.
ParticleLight particleLightAt(vec3 p) {
    vec3 sum;
    vec3 toward;
    ParticleLight lit;
    gatherParticleLight(p, lit.ambient, sum, toward);
    float pull    = length(toward);
    float total   = luma(sum);
    float aligned = total > 1e-6 ? min(pull / total, 1.0) : 0.0;
    lit.ambient  += sum * (1.0 - aligned) * 0.5;
    lit.color     = sum * aligned;
    lit.direction = pull > 1e-6 ? toward / pull : vec3(0.0, 1.0, 0.0);
    return lit;
}

// As a card takes it, for a strip: all of it, from every way alike.
vec3 particleCardLightAt(vec3 p) {
    vec3 ambient;
    vec3 sum;
    vec3 toward;
    gatherParticleLight(p, ambient, sum, toward);
    return ambient + sum;
}
