// 1 where the surface nearest the probe faces away from it, else 0. Drawn with
// culling off and the depth test on, so each texel is the closest surface. A
// probe seeing back faces in most directions is inside geometry; the SH
// projection reads this cube to refuse it. The radiance capture culls back
// faces, so from inside a solid it sees through the walls and cannot tell.
out vec4 FragColor;

void main() {
    FragColor = vec4(gl_FrontFacing ? 0.0 : 1.0);
}
