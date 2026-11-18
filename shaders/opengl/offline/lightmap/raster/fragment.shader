// Where a lightmap texel lies and the way it faces; w says a receiver covers it.
in vec3 vWorldPos;
in vec3 vNormal;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormal;

void main() {
    outPosition = vec4(vWorldPos, 1.0);
    outNormal   = vec4(normalize(vNormal), 1.0);
}
