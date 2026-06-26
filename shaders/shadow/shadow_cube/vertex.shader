
// Point-light cube shadow depth: rasterise into one cube face. The fragment
// stage writes linear distance-to-light, so the world position is passed down.
// The model matrix arrives per-instance (casters batched by mesh).
layout (location = 0) in vec3 aPos;
layout (location = 4) in mat4 aModel;  // per-instance model matrix (loc 4-7, divisor 1)

uniform mat4 u_faceVP;

out vec3 vWorldPos;

void main() {
    vec4 wp   = aModel * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    gl_Position = u_faceVP * wp;
}
