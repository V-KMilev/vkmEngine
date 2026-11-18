// An outline's rim, in one color: no light reaches it, and the reflection inputs under it are zero,
// so nothing is reflected onto it.
uniform vec4 u_color;  // its color over the exposure, and the alpha a step of the pass writes

layout(location = OUT_COLOR) out vec4 FragColor;
layout(location = OUT_REFLECT_WEIGHT) out vec4 ReflectWeight;
layout(location = OUT_REFLECT_ENV) out vec4 ReflectEnv;

void main() {
    FragColor     = u_color;
    ReflectWeight = vec4(0.0);
    ReflectEnv    = vec4(0.0);
}
