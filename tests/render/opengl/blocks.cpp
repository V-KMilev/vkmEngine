// The blocks C++ fills and the shaders read: laid out by two compilers that never meet, so a member
// moved on one side reads wrong bytes on the other, silently. Asked of the linked programs, the layout
// the driver really uses.

#include <cstddef>
#include <cstdio>

#include <GL/glew.h>

#include "vkm_opengl/shader/shader.h"

#include "asset/gl_material.h"
#include "frame/gl_camera.h"
#include "frame/light/gl_lights.h"
#include "frame/light/gl_shadow_block.h"
#include "frame/water/gl_water_bodies.h"
#include "frame/water/gl_waves.h"
#include "offline/gl_probe_manager.h"
#include "storage/light/gl_cluster_grid.h"
#include "storage/light/gl_lightmaps.h"
#include "debug/draw/debug_line.h"
#include "system/render/data/particle_data.h"

#include "harness/check.h"
#include "render/opengl/gpu.h"

using namespace Vkm::Test;

namespace {

/// @p property of the resource @p name of interface @p kind in @p shader's linked program; -1 for none.
GLint resource(const Vkm::OpenGL::Shader& shader, GLenum kind, const char* name, GLenum property) {
    shader.bind();
    const GLuint program = shader.getID();
    const GLuint index   = glGetProgramResourceIndex(program, kind, name);
    if (index == GL_INVALID_INDEX) return -1;
    GLint value = -1;
    glGetProgramResourceiv(program, kind, index, 1, &property, 1, nullptr, &value);
    return value;
}

} // namespace

TEST("the blocks C++ fills are laid out as the shaders read them: each block's size or stride, and its members") {
    const Probes gpu;
    const Vkm::OpenGL::Shader pbr("shaders/opengl/scene/forward/pbr");
    const Vkm::OpenGL::Shader baked("shaders/opengl/scene/forward/pbr", {"BAKED"});
    const Vkm::OpenGL::Shader water("shaders/opengl/scene/forward/water");
    const Vkm::OpenGL::Shader particle("shaders/opengl/scene/particle");
    const Vkm::OpenGL::Shader strip("shaders/opengl/scene/particle/strip");
    const Vkm::OpenGL::Shader lines("shaders/opengl/overlay/debug_draw");

    constexpr GLenum UBO    = GL_UNIFORM_BLOCK;
    constexpr GLenum MEMBER = GL_UNIFORM;
    constexpr GLenum SIZE   = GL_BUFFER_DATA_SIZE;
    constexpr GLenum AT     = GL_OFFSET;
    constexpr GLenum RECORD = GL_BUFFER_VARIABLE;
    constexpr GLenum STRIDE = GL_TOP_LEVEL_ARRAY_STRIDE;
    constexpr size_t TILE   = sizeof(Shadow2DGPU);
    constexpr size_t LIGHTS = offsetof(LightsBuffer, lights);
    using Lightmap = GLLightmaps::ObjectLightmap;

    struct Fact {
        const Vkm::OpenGL::Shader& program;
        GLenum                     kind;
        const char*                name;
        GLenum                     property;
        size_t                     cpp;
    };
    const Fact facts[] = {
        {pbr,      UBO,    "ShadowBlock",                                SIZE,   sizeof(ShadowUBOData)},
        {pbr,      MEMBER, "ShadowBlock.cascadeSplits",                  AT,     offsetof(ShadowUBOData, cascadeSplits)},
        {pbr,      MEMBER, "ShadowBlock.csmBase",                        AT,     offsetof(ShadowUBOData, csmBase)},
        {pbr,      MEMBER, "ShadowBlock.moverCount",                     AT,     offsetof(ShadowUBOData, moverCount)},
        {pbr,      MEMBER, "ShadowBlock.held[0].params",                 AT,     offsetof(ShadowUBOData, held) + offsetof(Shadow2DGPU, params)},
        {pbr,      MEMBER, "ShadowBlock.s2d[0].params",                  AT,     offsetof(ShadowUBOData, s2d) + offsetof(Shadow2DGPU, params)},
        {pbr,      MEMBER, "ShadowBlock.s2d[1].shape",                   AT,     offsetof(ShadowUBOData, s2d) + TILE + offsetof(Shadow2DGPU, shape)},
        {pbr,      MEMBER, "ShadowBlock.scube[0].params",                AT,     offsetof(ShadowUBOData, scube) + offsetof(ShadowCubeGPU, params)},
        {pbr,      RECORD, "LightsBlock.lights[0].position",             STRIDE, sizeof(GpuLight)},
        {pbr,      RECORD, "LightsBlock.lights[0].position",             AT,     LIGHTS},
        {pbr,      RECORD, "LightsBlock.lights[0].spot",                 AT,     LIGHTS + offsetof(GpuLight, spot)},
        {pbr,      RECORD, "LightsBlock.lights[0].bake",                 AT,     LIGHTS + offsetof(GpuLight, bake)},
        {pbr,      UBO,    "CameraBlock",                                SIZE,   sizeof(CameraUBO)},
        {pbr,      MEMBER, "CameraBlock.cameraPosition",                 AT,     offsetof(CameraUBO, cameraPosition)},
        {pbr,      MEMBER, "CameraBlock.viewport",                       AT,     offsetof(CameraUBO, viewport)},
        {pbr,      MEMBER, "CameraBlock.zFar",                           AT,     offsetof(CameraUBO, zFar)},
        {pbr,      MEMBER, "CameraBlock.viewmodelViewProjection",        AT,     offsetof(CameraUBO, viewmodelViewProjection)},
        {pbr,      UBO,    "MaterialBlock",                              SIZE,   sizeof(MaterialUBO)},
        {pbr,      MEMBER, "MaterialBlock.attenuationColor",             AT,     offsetof(MaterialUBO, attenuationColor)},
        {pbr,      MEMBER, "MaterialBlock.metallic",                     AT,     offsetof(MaterialUBO, metallic)},
        {pbr,      MEMBER, "MaterialBlock.alphaCutoff",                  AT,     offsetof(MaterialUBO, alphaCutoff)},
        {pbr,      MEMBER, "MaterialBlock.textureFlags",                 AT,     offsetof(MaterialUBO, textureFlags)},
        {pbr,      MEMBER, "MaterialBlock.boxMapping",                   AT,     offsetof(MaterialUBO, boxMapping)},
        {pbr,      UBO,    "ProbeBlock",                                 SIZE,   sizeof(ProbeBlock)},
        {pbr,      MEMBER, "ProbeBlock.probes[0].params",                AT,     offsetof(GpuProbe, params)},
        {pbr,      MEMBER, "ProbeBlock.probes[1].center",                AT,     sizeof(GpuProbe)},
        {pbr,      RECORD, "ClusterGrid.clusters[0].count",              STRIDE, GLClusterGrid::CLUSTER_STRIDE},
        {baked,    RECORD, "b_lightmaps[0].scaleOffset",                 STRIDE, sizeof(Lightmap)},
        {baked,    RECORD, "b_lightmaps[0].scaleOffset",                 AT,     offsetof(Lightmap, scaleOffset)},
        {baked,    RECORD, "b_lightmaps[0].page",                        AT,     offsetof(Lightmap, page)},
        {water,    UBO,    "WaveBlock",                                  SIZE,   sizeof(WaveUBO)},
        {water,    MEMBER, "WaveBlock.squeeze",                          AT,     offsetof(WaveUBO, squeeze)},
        {water,    MEMBER, "WaveBlock.swell",                            AT,     offsetof(WaveUBO, swell)},
        {water,    MEMBER, "WaveBlock.surges[0]",                        AT,     offsetof(WaveUBO, surges)},
        {water,    RECORD, "b_waterBodies[0].shares",                    STRIDE, sizeof(WaterBodyGPU)},
        {water,    RECORD, "b_waterBodies[0].shore",                     AT,     offsetof(WaterBodyGPU, shore)},
        {particle, RECORD, "ParticleBlock.particles[0].positionSize",    STRIDE, sizeof(ParticleData)},
        {particle, RECORD, "ParticleBlock.particles[0].params",          AT,     offsetof(ParticleData, params)},
        {particle, RECORD, "ParticleBlock.particles[0].uvRect",          AT,     offsetof(ParticleData, uvRect)},
        {particle, RECORD, "ParticleBlock.particles[0].uvNext",          AT,     offsetof(ParticleData, uvNext)},
        {strip,    RECORD, "StripBlock.points[0].positionWidth",         STRIDE, sizeof(StripPoint)},
        {strip,    RECORD, "StripBlock.points[0].params",                AT,     offsetof(StripPoint, params)},
        {strip,    RECORD, "StripBlock.points[0].look",                  AT,     offsetof(StripPoint, look)},
        {lines,    RECORD, "DebugLineBlock.lines[0].a",                  STRIDE, sizeof(DebugLine)},
        {lines,    RECORD, "DebugLineBlock.lines[0].b",                  AT,     offsetof(DebugLine, b)},
        {lines,    RECORD, "DebugLineBlock.lines[0].color",              AT,     offsetof(DebugLine, color)},
        {lines,    RECORD, "DebugLineBlock.lines[0].flags",              AT,     offsetof(DebugLine, flags)},
    };

    int apart = 0;
    for (const Fact& fact : facts) {
        const GLint shader = resource(fact.program, fact.kind, fact.name, fact.property);
        if (shader == static_cast<GLint>(fact.cpp)) continue;
        ++apart;
        std::printf("  %s: the program has %d, C++ %zu\n", fact.name, shader, fact.cpp);
    }
    std::printf("  %d of %zu apart\n", apart, std::size(facts));
    CHECK(apart == 0);
}
