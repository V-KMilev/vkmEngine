/**
 * Does not compile, on purpose: tests/render/render_tests.cpp checks when Vkm::GL::ShaderBase says so.
 */
layout(local_size_x = 1) in;

void main() {
    this is not glsl;
}
