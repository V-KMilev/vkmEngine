/**
 * PBR Vertex Shader - Instanced Rendering
 *
 * Transforms vertices and prepares data for PBR fragment shader.
 * Uses GPU instancing: model matrices are passed via per-instance
 * vertex attributes (locations 4-7) rather than uniforms.
 *
 * Attribute Layout:
 *   0-3: Per-vertex (position, normal, uv, tangent)
 *   4-7: Per-instance (model matrix columns, divisor=1)
 */
#version 420 core

// Per-vertex attributes (from mesh VBO)
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNorm;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec4 aTangent;     // xyz = tangent, w = handedness

// Per-instance model matrix (from instance buffer, divisor=1)
layout (location = 4) in vec4 aModelCol0;
layout (location = 5) in vec4 aModelCol1;
layout (location = 6) in vec4 aModelCol2;
layout (location = 7) in vec4 aModelCol3;

uniform mat4 u_viewProjection;

// Outputs to fragment shader
out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out vec3 Tangent;
out vec3 Bitangent;

void main() {
    // Reconstruct model matrix from instance attributes
    mat4 model = mat4(aModelCol0, aModelCol1, aModelCol2, aModelCol3);

    // Transform position to world space
    FragPos = vec3(model * vec4(aPos, 1.0));

    // Normal matrix: mat3(model) is correct for uniform scale (no shear/non-uniform).
    // Avoids per-vertex inverse() (~30 FLOPs). If non-uniform scale is needed later,
    // precompute the normal matrix on CPU as an additional instance attribute.
    mat3 normalMatrix = mat3(model);
    Normal = normalize(normalMatrix * aNorm);

    // Pass through texture coordinates
    TexCoords = aUV;

    // Build TBN matrix for normal mapping
    Tangent = normalize(normalMatrix * aTangent.xyz);

    // Gram-Schmidt re-orthogonalization
    Tangent = normalize(Tangent - dot(Tangent, Normal) * Normal);

    // Bitangent with handedness correction
    Bitangent = cross(Normal, Tangent) * aTangent.w;

    // Final clip-space position
    gl_Position = u_viewProjection * model * vec4(aPos, 1.0);
}
