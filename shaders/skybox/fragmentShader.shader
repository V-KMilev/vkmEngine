/**
 * Skybox fragment shader.
 *
 * Either samples the baked environment cubemap or evaluates an analytic
 * Preetham daylight sky (driven by the scene sun direction + turbidity),
 * selected by u_proceduralSky. Outputs LINEAR radiance into the HDR target -
 * the composite pass owns exposure + AgX + sRGB, so the sky tone-maps
 * consistently with lit geometry.
 */
#version 420 core

in vec3 vDir;

out vec4 FragColor;

uniform samplerCube u_envCube;
uniform float u_iblIntensity;

uniform int   u_proceduralSky;  // 0 = HDRI cubemap, 1 = analytic sky
uniform vec3  u_sunDir;         // world-space direction TO the sun
uniform float u_turbidity;      // ~2 (clear) .. ~10 (hazy)
uniform float u_skyIntensity;

const float PI = 3.14159265359;

float perez(float A, float B, float C, float D, float E,
            float cosTheta, float gamma, float cosGamma) {
    return (1.0 + A * exp(B / max(cosTheta, 0.01)))
         * (1.0 + C * exp(D * gamma) + E * cosGamma * cosGamma);
}

// Preetham "A Practical Analytic Model for Daylight" (compact form).
vec3 preethamSky(vec3 dir, vec3 sunDir, float T) {
    float cosTheta = clamp(dir.y, 0.0, 1.0);
    float cosGamma = clamp(dot(dir, sunDir), -1.0, 1.0);
    float gamma    = acos(cosGamma);
    float thetaS   = acos(clamp(sunDir.y, 0.0, 1.0));
    float T2 = T * T;

    float AY =  0.1787 * T - 1.4630, BY = -0.3554 * T + 0.4275;
    float CY = -0.0227 * T + 5.3251, DY =  0.1206 * T - 2.5771;
    float EY = -0.0670 * T + 0.3703;

    float Ax = -0.0193 * T - 0.2592, Bx = -0.0665 * T + 0.0008;
    float Cx = -0.0004 * T + 0.2125, Dx = -0.0641 * T - 0.8989;
    float Ex = -0.0033 * T + 0.0452;

    float Ay = -0.0167 * T - 0.2608, By = -0.0950 * T + 0.0092;
    float Cy = -0.0079 * T + 0.2102, Dy = -0.0441 * T - 1.6537;
    float Ey = -0.0109 * T + 0.0529;

    float chi = (4.0 / 9.0 - T / 120.0) * (PI - 2.0 * thetaS);
    float Yz  = (4.0453 * T - 4.9710) * tan(chi) - 0.2155 * T + 2.4192;

    float ts1 = thetaS, ts2 = ts1 * ts1, ts3 = ts2 * ts1;
    float xz = ( 0.00166 * ts3 - 0.00375 * ts2 + 0.00209 * ts1) * T2
             + (-0.02903 * ts3 + 0.06377 * ts2 - 0.03202 * ts1 + 0.00394) * T
             + ( 0.11693 * ts3 - 0.21196 * ts2 + 0.06052 * ts1 + 0.25886);
    float yz = ( 0.00275 * ts3 - 0.00610 * ts2 + 0.00317 * ts1) * T2
             + (-0.04214 * ts3 + 0.08970 * ts2 - 0.04153 * ts1 + 0.00516) * T
             + ( 0.15346 * ts3 - 0.26756 * ts2 + 0.06670 * ts1 + 0.26688);

    float cosThetaS = cos(thetaS);
    float Y = Yz * perez(AY,BY,CY,DY,EY, cosTheta, gamma, cosGamma)
                 / perez(AY,BY,CY,DY,EY, 1.0, thetaS, cosThetaS);
    float x = xz * perez(Ax,Bx,Cx,Dx,Ex, cosTheta, gamma, cosGamma)
                 / perez(Ax,Bx,Cx,Dx,Ex, 1.0, thetaS, cosThetaS);
    float y = yz * perez(Ay,By,Cy,Dy,Ey, cosTheta, gamma, cosGamma)
                 / perez(Ay,By,Cy,Dy,Ey, 1.0, thetaS, cosThetaS);

    float Yl = Y * 0.06;  // map model luminance into a usable HDR range
    float invY = 1.0 / max(y, 1e-4);
    float X = (x * invY) * Yl;
    float Z = ((1.0 - x - y) * invY) * Yl;

    vec3 rgb = vec3(
         3.2406 * X - 1.5372 * Yl - 0.4986 * Z,
        -0.9689 * X + 1.8758 * Yl + 0.0415 * Z,
         0.0557 * X - 0.2040 * Yl + 1.0570 * Z);
    return max(rgb, vec3(0.0));
}

void main() {
    vec3 dir = normalize(vDir);
    vec3 color;
    if (u_proceduralSky == 1) {
        color = preethamSky(dir, normalize(u_sunDir), u_turbidity) * u_skyIntensity;
    } else {
        color = texture(u_envCube, dir).rgb * u_iblIntensity;
    }
    FragColor = vec4(color, 1.0);
}
