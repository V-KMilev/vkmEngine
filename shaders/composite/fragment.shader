#version 430 core

in vec2 vUV;
out vec4 FragColor;

layout(binding = 0) uniform sampler2D u_hdr;  // linear HDR scene

void main() {
    vec3 color = texture(u_hdr, vUV).rgb;

    // Reinhard tonemap + gamma to the LDR backbuffer.
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, 1.0);
}
