#include "offline/gl_cube_convolver.h"

#include <GL/glew.h>

#include "gl_context.h"

#include "asset/gl_mesh.h"
#include "offline/gl_cubemap.h"

namespace Vkm::Engine {

GLCubeConvolver::GLCubeConvolver(const GLMesh& cube)
    : m_irradiance("shaders/ibl/irradiance")
    , m_prefilter("shaders/ibl/prefilter")
    , m_cube(cube)
    , m_projection(GLCubemap::convolveProjection())
{
    // Direction-only views (the convolution integrates over directions, so the
    // cube is sampled about the origin). Constant per face - compute once.
    for (int face = 0; face < 6; ++face) m_faceViews[face] = GLCubemap::faceView(face, glm::vec3(0.0f));
}

GLCubeConvolver::~GLCubeConvolver() = default;

void GLCubeConvolver::irradianceFace(int face, Target& target) {
    m_irradiance.bind();
    m_irradiance.setUniformMatrix4fv("u_projection", m_projection);
    m_irradiance.setUniformMatrix4fv("u_view", m_faceViews[face]);
    target.attachIrradiance(face);
    m_cube.draw();
}

void GLCubeConvolver::prefilterBand(
    Vkm::GL::Context& gl,
    int face,
    int mip,
    int mips,
    int size,
    int band,
    int bands,
    Target& target
) {
    m_prefilter.bind();
    m_prefilter.setUniformMatrix4fv("u_projection", m_projection);
    m_prefilter.setUniformMatrix4fv("u_view", m_faceViews[face]);
    m_prefilter.setUniform1f("u_roughness", roughnessOf(mip, mips));
    target.attachPrefilter(face, mip);

    const int first = size * band / bands;
    gl.enableScissor(true);
    gl.setScissor(0, first, size, size * (band + 1) / bands - first);
    m_cube.draw();
    gl.enableScissor(false);
}

std::vector<GLCubeConvolver::Step> GLCubeConvolver::slices(int prefilterSize, int mips) {
    using Kind = Step::Kind;
    std::vector<Step> steps;
    steps.push_back({Kind::Mirror, 0, 0, prefilterSize, mips, 0, 1});
    for (int face = 0; face < 6; ++face) steps.push_back({Kind::Irradiance, face, 0, 0, mips, 0, 1});
    for (int face = 0; face < 6; ++face) {
        for (int mip = 1; mip < mips; ++mip) {
            const int size  = prefilterSize >> mip;
            const int parts = bands(size);
            for (int part = 0; part < parts; ++part) {
                steps.push_back({Kind::Prefilter, face, mip, size, mips, part, parts});
            }
        }
    }
    return steps;
}

void GLCubeConvolver::run(Vkm::GL::Context& gl, const Step& step, Target& target) {
    if (step.kind == Step::Kind::Mirror) target.makeSourceMips();
    target.bindSource();
    switch (step.kind) {
        case Step::Kind::Mirror:
            // Every face of it, whole: a mirror is one read a texel.
            for (int face = 0; face < 6; ++face) {
                prefilterBand(gl, face, 0, step.mips, step.size, 0, 1, target);
            }
            break;
        case Step::Kind::Irradiance:
            irradianceFace(step.face, target);
            break;
        case Step::Kind::Prefilter:
            prefilterBand(gl, step.face, step.mip, step.mips, step.size, step.part, step.parts, target);
            break;
    }
}

void GLCubeConvolver::convolve(Vkm::GL::Context& gl, int prefilterSize, int mips, Target& target) {
    for (const Step& step : slices(prefilterSize, mips)) run(gl, step, target);
}

float GLCubeConvolver::roughnessOf(int mip, int mips) {
    return mips > 1 ? static_cast<float>(mip) / static_cast<float>(mips - 1) : 0.0f;
}

} // namespace Vkm::Engine
