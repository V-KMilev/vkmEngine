#include "egl_context.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <GL/glew.h>

namespace Vkm::Test {

namespace {

// The minimum the engine targets. Asking for it here is also how the tests
// answer whether the floor could be raised: a driver that cannot give a 4.5 core
// context is a driver the engine could not run on either.
constexpr EGLint GL_MAJOR = 4;
constexpr EGLint GL_MINOR = 5;

const char* glString(GLenum name) {
    const GLubyte* s = glGetString(name);
    return s ? reinterpret_cast<const char*>(s) : "";
}

} // namespace

GLContext::GLContext() {
    auto queryDevices = reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(
        eglGetProcAddress("eglQueryDevicesEXT"));
    auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));

    if (!queryDevices || !getPlatformDisplay) {
        m_reason = "EGL_EXT_platform_device is not available";
        return;
    }

    // Every device, not the first: this machine reports three and only one of
    // them is the GPU - the others are software rasterisers that either fail to
    // initialise or cannot give a core context.
    EGLDeviceEXT devices[8];
    EGLint       deviceCount = 0;
    queryDevices(8, devices, &deviceCount);
    if (deviceCount <= 0) {
        m_reason = "EGL reports no devices";
        return;
    }

    for (EGLint i = 0; i < deviceCount; ++i) {
        EGLDisplay display = getPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, devices[i], nullptr);
        if (display == EGL_NO_DISPLAY) continue;

        EGLint major = 0;
        EGLint minor = 0;
        if (!eglInitialize(display, &major, &minor)) continue;

        if (!eglBindAPI(EGL_OPENGL_API)) {
            eglTerminate(display);
            continue;
        }

        const EGLint contextAttribs[] = {
            EGL_CONTEXT_MAJOR_VERSION,        GL_MAJOR,
            EGL_CONTEXT_MINOR_VERSION,        GL_MINOR,
            EGL_CONTEXT_OPENGL_PROFILE_MASK,  EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
            EGL_NONE
        };
        // No config and no surface: nothing is presented, and every target the
        // tests render into is one they made themselves.
        EGLContext context =
            eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, contextAttribs);
        if (context == EGL_NO_CONTEXT) {
            eglTerminate(display);
            continue;
        }

        if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
            eglDestroyContext(display, context);
            eglTerminate(display);
            continue;
        }

        // The engine reaches GL through glew, so the tests must too, or every
        // entry point the wrappers call is null.
        glewExperimental = GL_TRUE;
        const GLenum glewStatus = glewInit();
        // GLEW_ERROR_NO_GLX_DISPLAY is glew noticing there is no GLX display,
        // which is the whole point of an EGL context. Every entry point still
        // loads, so it is the one failure worth ignoring.
        if (glewStatus != GLEW_OK && glewStatus != GLEW_ERROR_NO_GLX_DISPLAY) {
            m_reason = std::string("glew could not load the entry points: ")
                     + reinterpret_cast<const char*>(glewGetErrorString(glewStatus));
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(display, context);
            eglTerminate(display);
            continue;
        }
        // glewExperimental provokes a GL_INVALID_ENUM that means nothing; clear
        // it, or the first test to check glGetError inherits it.
        glGetError();

        m_display   = display;
        m_context   = context;
        m_version   = glString(GL_VERSION);
        m_renderer  = glString(GL_RENDERER);
        m_available = true;
        return;
    }

    if (m_reason.empty()) {
        m_reason = "no EGL device could give a " + std::to_string(GL_MAJOR) + "."
                 + std::to_string(GL_MINOR) + " core context";
    }
}

GLContext::~GLContext() {
    if (!m_display) return;
    EGLDisplay display = static_cast<EGLDisplay>(m_display);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (m_context) eglDestroyContext(display, static_cast<EGLContext>(m_context));
    eglTerminate(display);
}

} // namespace Vkm::Test
