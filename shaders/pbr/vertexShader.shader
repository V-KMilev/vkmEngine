#version 420 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNorm;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec4 aTangent;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_viewProjection;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out vec3 Tangent;
out vec3 Bitangent;

void main() {
    FragPos = vec3(u_model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(u_model))) * aNorm;
    TexCoords = aUV;

    // Calculate TBN matrix for normal mapping
    Tangent = normalize(mat3(transpose(inverse(u_model))) * aTangent.xyz);
    Normal = normalize(Normal);

    // Gram-Schmidt orthogonalization
    Tangent = normalize(Tangent - dot(Tangent, Normal) * Normal);
    Bitangent = cross(Normal, Tangent) * aTangent.w; // w component is handedness

    gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);
}

