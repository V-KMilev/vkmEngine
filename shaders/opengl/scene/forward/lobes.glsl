/*
 * A material's lobes for one light: GGX specular over Lambert diffuse, and the optional ones -
 * subsurface, thin transmission, clear coat and sheen - which a program compiles in or out by the
 * constant FEATURES it defines before including this.
 */

#include "shading.glsl"

// A clear coat is a dielectric of index 1.5.
const float COAT_F0 = 0.04;

// The direct lights' specular, summed apart as evaluateLight shades it, so a
// transmissive surface keeps its reflections when its color blends toward what it refracts.
vec3 g_directSpecular = vec3(0.0);

bool hasSubsurface() {
    return FEATURES && u_material.subsurface > 0.001;
}

// The sheen color's strongest channel, 0 for no sheen.
float sheenStrength() {
    vec3 sheenColor = u_material.sheenColor.rgb;
    return max(sheenColor.r, max(sheenColor.g, sheenColor.b));
}

bool hasSheen() {
    return FEATURES && sheenStrength() > 0.0;
}

// A clear coat lies over @p s, its mask sampled per fragment.
bool coated(Surface s) {
    return FEATURES && s.clearcoat > 0.001;
}

// The sheen lobe's directional albedo at this angle to the normal, from the BRDF LUT's third
// channel.
float sheenAlbedo(float NdotX) {
    return texture(u_brdfLUT, vec2(NdotX, u_material.sheenColor.a)).b;
}

// How much of the light a sheen takes at this angle, its albedo times its strength: the layer
// under it keeps the rest (KHR_materials_sheen's albedo scaling).
float sheenTaken(float NdotX) {
    return sheenStrength() * sheenAlbedo(NdotX);
}

// A GGX alpha widened by a source of tangent @p sourceTan (Karis 2013, Eq. 16).
float broadenedAlpha(float alpha, float sourceTan) {
    return clamp(alpha + sourceTan / 3.0, 0.0, 1.0);
}

// One light's reflected radiance toward the viewer, arriving along @p L with @p radiance.
// sourceTan is the tan of the light's angular radius (0 for a point); the lobe widens over the
// disc unrenormalized, spreading its irradiance instead of a peak a half float may not hold. A
// sphere light's lobe is toward its representative point @p Ls, renormalized by
// @p sphereNorm (sphereLobe); Ls is L for a point source.
vec3 evaluateLight(
    Shading sh,
    Surface s,
    vec3 L,
    vec3 radiance,
    float sourceTan,
    vec3 Ls,
    float sphereNorm
) {
    vec3  N     = sh.N;
    vec3  V     = sh.V;
    vec3  H     = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float a = broadenedAlpha(s.roughness * s.roughness, sourceTan);

    // Base specular; specNoL is the cosine the lobe takes.
    vec3  Hs      = normalize(V + Ls);
    float specNoL = max(dot(N, Ls), 0.0);
    float D       = distributionGGX(max(dot(N, Hs), 0.0), a) * sphereNorm;
    float Vis     = visSmithCorrelated(sh.NdotV, specNoL, a);
    vec3 F = fresnelSchlick(VdotH, sh.f0);
    vec3 specular = D * Vis * F * sh.energyCompensation;

    // Subsurface wrap bleeds past the terminator, tinted there alone so the lit
    // side keeps the albedo.
    vec3 kd = (vec3(1.0) - F) * (1.0 - s.metallic);
    vec3 diffuse = kd * s.albedo / PI;
    float diffNoL = NdotL;
    vec3  diffTint = vec3(1.0);
    if (hasSubsurface()) {
        float w      = u_material.subsurface;
        float rawNoL = dot(N, L);
        diffNoL = clamp((rawNoL + w) / ((1.0 + w) * (1.0 + w)), 0.0, 1.0);
        float blend = 1.0 - smoothstep(-w, w, rawNoL);
        diffTint = mix(vec3(1.0), u_material.subsurfaceColor.rgb, blend);
    }

    // Thin transmission trades front diffuse for a back-lit term, tinted by
    // Beer-Lambert absorption when a volume is declared.
    vec3 transmitted = vec3(0.0);
    if (FEATURES && s.transmission > 0.001) {
        float kt = s.transmission * (1.0 - s.metallic);
        diffuse *= (1.0 - kt);
        transmitted = kt * s.albedo / PI * max(dot(-N, L), 0.0);
        if (u_material.thicknessFactor > 0.0) {
            vec3 transmittance = pow(
                max(u_material.attenuationColor.rgb, vec3(1e-4)),
                vec3(u_material.thicknessFactor / max(u_material.attenuationColor.a, 1e-4))
            );
            transmitted *= transmittance;
        }
    }

    // Subsurface back translucency, tinted: a wrapped back-light through a broad lobe about the
    // light's own direction, so a leaf glows looking toward the sun and not with it behind.
    // Normalized as the diffuse beside it is.
    vec3 sss = vec3(0.0);
    if (hasSubsurface()) {
        float wrap    = clamp((-dot(N, L) + 0.5) / 2.25, 0.0, 1.0);
        float scatter = distributionGGX(clamp(-dot(V, L), 0.0, 1.0), 0.6);
        sss = u_material.subsurfaceColor.rgb * s.albedo * (wrap * scatter * u_material.subsurface);
    }

    vec3 baseLit = diffuse * diffTint * diffNoL + specular * specNoL;

    // Clearcoat: a dielectric GGX lobe dimming the base by its Fresnel, on the geometric normal -
    // smooth over the base's bumps (KHR_materials_clearcoat without a coat normal map).
    vec3 ccContrib = vec3(0.0);
    float underCoat = 1.0;
    float ccStrength = s.clearcoat;
    if (FEATURES && ccStrength > 0.001) {
        vec3  Ng   = sh.Ng;
        float cNoL = max(dot(Ng, L), 0.0);
        float cNoV = max(dot(Ng, V), 1e-4);
        float cca = broadenedAlpha(s.clearcoatRoughness * s.clearcoatRoughness, sourceTan);
        float ccD = distributionGGX(max(dot(Ng, H), 0.0), cca);
        float ccV = visSmithCorrelated(cNoV, cNoL, cca);
        float ccF = fresnelSchlick(VdotH, vec3(COAT_F0)).x * ccStrength;
        underCoat = 1.0 - ccF;
        baseLit *= underCoat;
        ccContrib = vec3(ccD * ccV * ccF) * cNoL;
    }

    // A sheen over the base and under the coat, off when its color is black. What it takes of the
    // light going in or coming out the base below does not get (KHR_materials_sheen).
    vec3  sheen      = vec3(0.0);
    float underSheen = 1.0;
    if (hasSheen()) {
        float sheenAlpha = u_material.sheenColor.a * u_material.sheenColor.a;
        float sheenD     = distributionCharlie(NdotH, sheenAlpha);
        float sheenV     = visCharlie(sh.NdotV, NdotL, sheenAlpha);
        sheen      = u_material.sheenColor.rgb * (sheenD * sheenV * NdotL) * underCoat;
        underSheen = 1.0 - max(sheenTaken(sh.NdotV), sheenTaken(NdotL));
    }

    g_directSpecular += (specular * specNoL * underCoat * underSheen + ccContrib) * radiance;
    return ((baseLit + transmitted + sss) * underSheen + ccContrib + sheen) * radiance;
}
