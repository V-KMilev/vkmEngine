// The GPU-side suite. Everything here needs a live OpenGL context, which is why
// none of it could exist before: the engine's context comes from GLFW, GLFW
// needs a window system, and a build machine has none. Vkm::Test::GLContext
// opens the device through EGL instead.
//
// It skips rather than fails where there is no GPU, so this binary stays green
// on a machine that cannot run it - the same contract vkm_gl_tests states for
// itself, kept by not requiring what is not there.

#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <GL/glew.h>

#include "logger.h"

#include "egl_context.h"

#include "gl_vertex_buffer.h"
#include "gl_index_buffer.h"
#include "gl_shader_storage_buffer.h"
#include "gl_shader.h"
#include "gl_compute_shader.h"
#include "gl_shader_preprocess.h"

#include "gl_context.h"
#include "gl_backend.h"
#include "platform/window/window_manager.h"

namespace {

int g_failures = 0;

void check(const char* what, bool ok) {
    if (!ok) ++g_failures;
    std::printf("  %-62s %s\n", what, ok ? "ok" : "<-- FAILED");
}

// A GL error left set by one test is a failure reported by the next one, so
// every test that cares reads and clears rather than assuming.
bool noGlError() { return glGetError() == GL_NO_ERROR; }

void testTheContextItself(const Vkm::Test::GLContext& gl) {
    std::printf("The context the tests run against:\n");
    check("it reports a version", !gl.version().empty());
    std::printf("      %s\n      %s\n", gl.version().c_str(), gl.renderer().c_str());

    // The engine's floor. Asking the driver rather than trusting the header is
    // the point: this is what says whether raising it is a real option.
    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    check("at or above the 4.3 the engine targets",
          major > 4 || (major == 4 && minor >= 3));
    check("and it is a core profile context", noGlError());

    GLint mask = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &mask);
    check("  which the profile mask confirms", (mask & GL_CONTEXT_CORE_PROFILE_BIT) != 0);
}

void testAVertexBufferRoundTrips() {
    std::printf("A vertex buffer, written and read back:\n");

    const std::vector<float> vertices = {
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
         0.0f,  0.5f, 0.0f,
    };
    const uint32_t bytes = static_cast<uint32_t>(vertices.size() * sizeof(float));

    Vkm::GL::VertexBuffer buffer(vertices.data(), bytes);
    check("it takes an id from the driver", buffer.getID() != 0);
    check("and reports the size it was given", buffer.getSize() == bytes);
    check("with no GL error behind it", noGlError());

    // The read-back is the part worth having: it says the bytes reached the GPU,
    // which no amount of checking return codes on this side can.
    std::vector<float> readBack(vertices.size(), 0.0f);
    buffer.bind();
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, bytes, readBack.data());
    buffer.unbind();
    check("and the data that comes back is the data that went up",
          readBack == vertices);
}

void testAnIndexBufferKnowsItsCount() {
    std::printf("An index buffer:\n");

    const std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};
    Vkm::GL::IndexBuffer buffer(indices.data(),
                                static_cast<uint32_t>(indices.size()));
    check("it takes an id", buffer.getID() != 0);
    check("and counts indices rather than bytes", buffer.getCount() == indices.size());
    check("with no GL error behind it", noGlError());
}

void testAShaderStorageBufferBindsToItsPoint() {
    std::printf("A shader storage buffer:\n");

    const std::vector<uint32_t> payload(64, 7u);
    const uint32_t bytes = static_cast<uint32_t>(payload.size() * sizeof(uint32_t));

    Vkm::GL::ShaderStorageBuffer ssbo(payload.data(), bytes);
    check("it takes an id", ssbo.getID() != 0);

    // Binding to an indexed point is what the compute passes actually do, and
    // it is the call that fails silently when a target argument is wrong - the
    // bug that made every indirect draw a no-op once.
    ssbo.bindBase(3);
    check("and binds to an indexed point without error", noGlError());

    GLint bound = 0;
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 3, &bound);
    check("  and the driver agrees it is bound there",
          bound == static_cast<GLint>(ssbo.getID()));
}

// Every shader the engine ships, compiled and linked exactly as the backend
// compiles them - same loader, same prelude, same GL version.
//
// This is the only thing that can say the shaders still build. They are not
// compiled by CMake, so nothing in the build touches them; the first reader is
// GLBackend's constructor, which is a window away from any build machine. A
// constant renamed in C++ or an #include removed from a .glsl was a black
// viewport somebody found by looking.
void testEveryShippedShaderCompiles() {
    std::printf("Every shader the engine ships:\n");

    // The loader resolves shader paths against the working directory, which is
    // what the hosts pin to the engine root at startup.
    std::error_code ec;
    std::filesystem::current_path(VKM_ENGINE_ROOT, ec);
    if (ec) {
        check("the engine root is reachable", false);
        return;
    }

    // The backend's own prelude: the #version it asks the context for, and the
    // constants it writes out of the C++ ones.
    Vkm::GL::setShaderPrelude(Vkm::Engine::OPENGL_GLSL_VERSION,
                              Vkm::Engine::GLBackend::shaderConstants());

    static const char* const GRAPHICS[] = {
        "shaders/bloom/down",     "shaders/bloom/up",
        "shaders/composite",      "shaders/decal",
        "shaders/dof",            "shaders/fog/apply",
        "shaders/forward/pbr",    "shaders/forward/pbr_skinned",
        "shaders/forward/prepass","shaders/forward/prepass_skinned",
        "shaders/grid",           "shaders/gtao",
        "shaders/hiz/reduce",     "shaders/ibl/brdf",
        "shaders/ibl/equirect",   "shaders/ibl/irradiance",
        "shaders/ibl/prefilter",  "shaders/ibl/sky",
        "shaders/particle",       "shaders/shadow/shadow_2d",
        "shaders/shadow/shadow_2d_skinned",
        "shaders/shadow/shadow_cube",
        "shaders/shadow/shadow_cube_skinned",
        "shaders/skybox",         "shaders/splash",
        "shaders/ui",
    };
    static const char* const COMPUTE[] = {
        "shaders/clustering",     "shaders/fog/inject",
        "shaders/fog/integrate",  "shaders/irradiance/project",
        "shaders/occlusion_cull",
    };

    int failed = 0;
    for (const char* path : GRAPHICS) {
        try {
            const Vkm::GL::Shader program(path);
            if (!program.isValid()) { std::printf("      %s did not link\n", path); ++failed; }
        } catch (const std::exception& e) {
            std::printf("      %s: %s\n", path, e.what());
            ++failed;
        }
    }
    for (const char* path : COMPUTE) {
        try {
            const Vkm::GL::ComputeShader program(path);
            if (!program.isValid()) { std::printf("      %s did not link\n", path); ++failed; }
        } catch (const std::exception& e) {
            std::printf("      %s: %s\n", path, e.what());
            ++failed;
        }
    }

    std::printf("      %zu graphics + %zu compute programs\n",
                sizeof(GRAPHICS) / sizeof(GRAPHICS[0]),
                sizeof(COMPUTE) / sizeof(COMPUTE[0]));
    check("all of them compile and link against the backend's own prelude", failed == 0);
}

// The constants a shader names have to be installed before anything compiles,
// and a backend builds programs in its members' constructors - so an install
// that waits for init() is one the first of them has already run past. Nothing
// else catches that: the suite above sets the prelude itself, and a host that
// does not is a host whose first shader is missing every constant it names.
void testABackendInstallsItsConstantsBeforeItCompilesAnything() {
    std::printf("A backend built the way a host builds one:\n");

    Vkm::GL::setShaderPrelude(Vkm::Engine::OPENGL_GLSL_VERSION, {});

    bool built = true;
    try {
        const Vkm::Engine::GLBackend backend;
        (void)backend;
    } catch (const std::exception& e) {
        std::printf("      %s\n", e.what());
        built = false;
    }
    check("its own programs compile, with no host having set the prelude", built);
}

} // namespace

int main() {
    Vkm::Log::Logger::init("/tmp/vkm_render_tests.log", "VKM-RENDER-TESTS",
                           Vkm::Log::LogLevel::ERROR);

    Vkm::Test::GLContext gl;
    if (!gl.available()) {
        // Not a failure. This binary is expected to run where there is no GPU.
        std::printf("No GL context: %s\nSkipping the GPU suite.\n", gl.reason().c_str());
        return 0;
    }

    testTheContextItself(gl);
    testAVertexBufferRoundTrips();
    testAnIndexBufferKnowsItsCount();
    testAShaderStorageBufferBindsToItsPoint();
    testEveryShippedShaderCompiles();
    testABackendInstallsItsConstantsBeforeItCompilesAnything();

    std::printf(g_failures ? "\n%d FAILURE(S)\n" : "\nALL OK\n", g_failures);
    return g_failures ? 1 : 0;
}
