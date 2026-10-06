/**
 * A mesh at its vertex positions, carrying its instance's object index; what
 * tests/render/render_tests.cpp checks a multi-draw by.
 */

layout(location = ATTR_POSITION) in vec3 aPos;
layout(location = ATTR_INSTANCE) in uint aInstance;

flat out uint vObject;

void main() {
    vObject     = aInstance;
    gl_Position = vec4(aPos, 1.0);
}
