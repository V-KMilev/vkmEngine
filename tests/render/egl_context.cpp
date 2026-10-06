#include "egl_context.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <GL/glew.h>

#include "platform/window/window_manager.h"

namespace Vkm::Test {

namespace {

// The engine's own version, so a suite never skips on a driver the engine runs on.
constexpr EGLint GL_MAJOR = Vkm::Engine::OPENGL_MAJOR_VERSION;
constexpr EGLint GL_MINOR = Vkm::Engine::OPENGL_MINOR_VERSION;

const char* glString(GLenum name) {
    const GLubyte* s = glGetString(name);
    return s ? reinterpret_cast<const char*>(s) : "";
}

} // namespace

GLContext::GLContext() {
    auto queryDevices = reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(eglGetProcAddress("eglQueryDevicesEXT"));
    auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT")
    );

    if (!queryDevices || !getPlatformDisplay) {
        m_reason = "EGL_EXT_platform_device is not available";
        return;
    }

    // Every device, not the first: software rasterisers listed beside the GPU may
    // fail to initialise or give no core context.
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
        // No config and no surface: nothing is presented; tests render into their own targets.
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

        // The engine reaches GL through glew, so the tests must too.
        glewExperimental = GL_TRUE;
        const GLenum glewStatus = glewInit();
        // GLEW_ERROR_NO_GLX_DISPLAY only says there is no GLX display, as expected
        // under EGL; every entry point still loads.
        if (glewStatus != GLEW_OK && glewStatus != GLEW_ERROR_NO_GLX_DISPLAY) {
            m_reason = std::string("glew could not load the entry points: ")
                + reinterpret_cast<const char*>(glewGetErrorString(glewStatus));
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(display, context);
            eglTerminate(display);
            continue;
        }
        // glewExperimental provokes a meaningless GL_INVALID_ENUM; clear it for the first glGetError.
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

bool GLContext::attachSurface(int width, int height) {
    if (!m_available) return false;
    EGLDisplay display = static_cast<EGLDisplay>(m_display);

    const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig config = nullptr;
    EGLint    configCount = 0;
    if (!eglChooseConfig(display, configAttribs, &config, 1, &configCount) || configCount < 1) {
        return false;
    }

    const EGLint surfaceAttribs[] = { EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE };
    EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttribs);
    if (surface == EGL_NO_SURFACE) return false;

    // Made with no config, so it can be made current on a surface chosen after it.
    if (!eglMakeCurrent(display, surface, surface, static_cast<EGLContext>(m_context))) {
        eglDestroySurface(display, surface);
        return false;
    }
    if (m_surface) eglDestroySurface(display, static_cast<EGLSurface>(m_surface));
    m_surface = surface;

    // A context first made current with no surface keeps GL_NONE as the default
    // framebuffer's draw and read buffers; without these, draws go nowhere.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_BACK);
    return glGetError() == GL_NO_ERROR;
}

GLContext::~GLContext() {
    if (!m_display) return;
    EGLDisplay display = static_cast<EGLDisplay>(m_display);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (m_surface) eglDestroySurface(display, static_cast<EGLSurface>(m_surface));
    if (m_context) eglDestroyContext(display, static_cast<EGLContext>(m_context));
    eglTerminate(display);
}

} // namespace Vkm::Test
