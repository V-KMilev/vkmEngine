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

void GLCubeConvolver::irradiance(const AttachFace& attach) {
    m_irradiance.bind();
    m_irradiance.setUniformMatrix4fv("u_projection", m_projection);
    m_irradiance.setUniform1i("u_slice", 0);
    m_irradiance.setUniform1i("u_slices", 1);
    for (int face = 0; face < 6; ++face) {
        m_irradiance.setUniformMatrix4fv("u_view", m_faceViews[face]);
        attach(face);
        m_cube.draw();
    }
}

void GLCubeConvolver::prefilter(int mips, const AttachMipFace& attach) {
    m_prefilter.bind();
    m_prefilter.setUniformMatrix4fv("u_projection", m_projection);
    for (int mip = 0; mip < mips; ++mip) {
        m_prefilter.setUniform1f("u_roughness", roughnessOf(mip, mips));
        for (int face = 0; face < 6; ++face) {
            m_prefilter.setUniformMatrix4fv("u_view", m_faceViews[face]);
            attach(face, mip);
            m_cube.draw();
        }
    }
}

void GLCubeConvolver::irradianceSlice(
    Vkm::GL::Context& gl,
    int face,
    int slice,
    int slices,
    const AttachFace& attach
) {
    m_irradiance.bind();
    m_irradiance.setUniformMatrix4fv("u_projection", m_projection);
    m_irradiance.setUniformMatrix4fv("u_view", m_faceViews[face]);
    m_irradiance.setUniform1i("u_slice", slice);
    m_irradiance.setUniform1i("u_slices", slices);
    attach(face);
    if (slice > 0) {
        gl.setBlending(true);
        gl.setBlendFunc(GL_ONE, GL_ONE);
    }
    m_cube.draw();
    gl.setBlending(false);
}

void GLCubeConvolver::prefilterBand(
    Vkm::GL::Context& gl,
    int face,
    int mip,
    int mips,
    int size,
    int band,
    int bands,
    const AttachMipFace& attach
) {
    m_prefilter.bind();
    m_prefilter.setUniformMatrix4fv("u_projection", m_projection);
    m_prefilter.setUniformMatrix4fv("u_view", m_faceViews[face]);
    m_prefilter.setUniform1f("u_roughness", roughnessOf(mip, mips));
    attach(face, mip);

    const int first = size * band / bands;
    gl.enableScissor(true);
    gl.setScissor(0, first, size, size * (band + 1) / bands - first);
    m_cube.draw();
    gl.enableScissor(false);
}

float GLCubeConvolver::roughnessOf(int mip, int mips) {
    return mips > 1 ? static_cast<float>(mip) / static_cast<float>(mips - 1) : 0.0f;
}

} // namespace Vkm::Engine
