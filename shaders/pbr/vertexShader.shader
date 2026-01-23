#version 420 core

// Per-vertex attributes
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNorm;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec4 aTangent;

// Per-instance model matrix (4 columns, each a vec4)
layout (location = 4) in vec4 aModelCol0;
layout (location = 5) in vec4 aModelCol1;
layout (location = 6) in vec4 aModelCol2;
layout (location = 7) in vec4 aModelCol3;

uniform mat4 u_viewProjection;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out vec3 Tangent;
out vec3 Bitangent;

void main() {
    // Reconstruct model matrix from instance attributes
    mat4 model = mat4(aModelCol0, aModelCol1, aModelCol2, aModelCol3);

    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNorm;
    TexCoords = aUV;

    // Calculate TBN matrix for normal mapping
    Tangent = normalize(mat3(transpose(inverse(model))) * aTangent.xyz);
    Normal = normalize(Normal);

    // Gram-Schmidt orthogonalization
    Tangent = normalize(Tangent - dot(Tangent, Normal) * Normal);
    Bitangent = cross(Normal, Tangent) * aTangent.w; // w component is handedness

    gl_Position = u_viewProjection * model * vec4(aPos, 1.0);
}

