/**
 * PBR vertex shader - instanced.
 *
 * Per-vertex attributes are locations 0-3; the per-instance model matrix is
 * locations 4-7 (divisor 1), filled by the shared instance buffer. Outputs
 * world-space position and a TBN basis for the fragment shader. No lighting
 * or color transform happens here.
 */
#version 420 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec4 aTangent;   // xyz = tangent, w = handedness

layout (location = 4) in vec4 aModelCol0;
layout (location = 5) in vec4 aModelCol1;
layout (location = 6) in vec4 aModelCol2;
layout (location = 7) in vec4 aModelCol3;

layout(std140, binding = 2) uniform CameraBlock {
    mat4 viewProjection;
    vec4 cameraPosition;  // xyz = position, w = exposure
    vec4 ambient;         // xyz = color,    w = intensity
} u_camera;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vTangent;
out vec3 vBitangent;

void main() {
    mat4 model = mat4(aModelCol0, aModelCol1, aModelCol2, aModelCol3);

    vec4 worldPos = model * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;

    // mat3(model) is correct for uniform scale (no shear/non-uniform); avoids
    // a per-vertex inverse. Non-uniform scale would need a CPU normal matrix.
    mat3 normalMatrix = mat3(model);
    vNormal = normalize(normalMatrix * aNormal);

    vTangent = normalize(normalMatrix * aTangent.xyz);
    vTangent = normalize(vTangent - dot(vTangent, vNormal) * vNormal);
    vBitangent = cross(vNormal, vTangent) * aTangent.w;

    vUV = aUV;

    gl_Position = u_camera.viewProjection * worldPos;
}
