#define VKM_LOG_CATEGORY "RENDER_THREAD"

#include "platform/threading/render_thread.h"

#include <GL/glew.h>     // Pulled BEFORE profiler_gl so the Tracy GPU macros see glQueryCounter etc.
#include <GLFW/glfw3.h>

#include "logger.h"
#include "debug/profiler_gl.h"

namespace Engine {

RenderThread::RenderThread(GLFWwindow* window)
    : m_window(window)
{
    // Release the context from whoever made it current (main thread, at
    // window create). The worker will claim it once it starts.
    glfwMakeContextCurrent(nullptr);
    m_thread = std::thread([this]() { workerMain(); });
    LOG_INFO("Render thread started; GL context migrated off main");
}

RenderThread::~RenderThread() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();

    // Hand the context back to the caller (main thread) so any GL cleanup
    // performed during shutdown finds it current again.
    glfwMakeContextCurrent(m_window);
    LOG_INFO("Render thread stopped; GL context returned to main");
}

void RenderThread::executeFrame(std::function<void()> renderJob) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingJob = std::move(renderJob);
        m_jobReady   = true;
        m_jobDone    = false;
    }
    m_cv.notify_one();

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this]() { return m_jobDone; });
    }
}

void RenderThread::workerMain() {
    glfwMakeContextCurrent(m_window);

    // Tracy's GPU context is thread-local; the one GLBackend created on
    // main when the backend was constructed isn't visible from here, so
    // PROFILE_GPU_COLLECT (called by GLBackend::endFrame on this thread)
    // would dereference null. Re-init for this thread now that the GL
    // context is current. No-op when VKM_PROFILER is off.
    PROFILE_GPU_CONTEXT();

    while (true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() { return m_jobReady || !m_running; });
            if (!m_running && !m_jobReady) break;
            job          = std::move(m_pendingJob);
            m_pendingJob = nullptr;
            m_jobReady   = false;
        }

        // Execute outside the lock so the main thread can post the next
        // frame's job while this one runs - relevant once Phase 2B lifts
        // the wait in executeFrame.
        job();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_jobDone = true;
        }
        m_cv.notify_all();
    }

    glfwMakeContextCurrent(nullptr);
}

} // namespace Engine
