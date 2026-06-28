#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "data/gl_cube_convolver.h"

#include "data/gl_mesh.h"
#include "data/gl_cubemap.h"

#include "generator/mesh_generators.h"

namespace Engine {

GLCubeConvolver::GLCubeConvolver()
    : m_irradiance("shaders/ibl/irradiance")
    , m_prefilter("shaders/ibl/prefilter")
    , m_cube(std::make_unique<GLMesh>(generateCube()))
    , m_projection(GLCubemap::convolveProjection())
{
    // Direction-only views (the convolution integrates over directions, so the
    // cube is sampled about the origin). Constant per face - compute once.
    for (int face = 0; face < 6; ++face) m_faceViews[face] = GLCubemap::faceView(face, glm::vec3(0.0f));
}

GLCubeConvolver::~GLCubeConvolver() = default;

void GLCubeConvolver::irradiance(const BindEnv& bindEnv, const AttachFace& attach) {
    m_irradiance.bind();
    m_irradiance.setUniformMatrix4fv("u_projection", m_projection);
    bindEnv();
    for (int face = 0; face < 6; ++face) {
        m_irradiance.setUniformMatrix4fv("u_view", m_faceViews[face]);
        attach(face);
        m_cube->draw();
    }
}

void GLCubeConvolver::prefilter(int mips, const BindEnv& bindEnv, const AttachMipFace& attach) {
    m_prefilter.bind();
    m_prefilter.setUniformMatrix4fv("u_projection", m_projection);
    bindEnv();
    for (int mip = 0; mip < mips; ++mip) {
        const float roughness = mips > 1 ? static_cast<float>(mip) / static_cast<float>(mips - 1) : 0.0f;
        m_prefilter.setUniform1f("u_roughness", roughness);
        for (int face = 0; face < 6; ++face) {
            m_prefilter.setUniformMatrix4fv("u_view", m_faceViews[face]);
            attach(face, mip);
            m_cube->draw();
        }
    }
}

} // namespace Engine
