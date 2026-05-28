#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

struct GLFWwindow;

namespace Engine {

/**
 * @brief Dedicated thread that owns the rendering backend's context.
 *
 * On construction it takes the rendering API context from the calling
 * (main) thread and makes it current on a worker; on destruction it
 * releases the context back to the main thread. While alive, the
 * context is private to the worker - no other thread may issue
 * backend calls.
 *
 * postFrame() posts a job non-blocking; waitForFrame() is the explicit
 * sync point. Engine::run uses this to overlap the previous frame's
 * draws with the current frame's CPU work: it posts frame K, then runs
 * non-mutator systems and the next view build for K+1 on main while
 * frame K is in flight; the mutator phase waits for frame K to finish
 * so resource writes never race the render thread's reads.
 *
 * IMPORTANT: this MUST be constructed after every backend setup the
 * engine performs at boot (shader compilation, default scene, IBL
 * bake). Once the context is on the worker, any backend call from
 * another thread is undefined behaviour.
 */
class RenderThread {
    public:
        RenderThread() = delete;

        RenderThread(const RenderThread& other) = delete;
        RenderThread& operator=(const RenderThread& other) = delete;

        RenderThread(RenderThread && other) = delete;
        RenderThread& operator=(RenderThread && other) = delete;

        /**
         * @brief Take the backend's context from the caller's thread and
         *        spawn a worker that holds it.
         */
        explicit RenderThread(GLFWwindow* window);

        /**
         * @brief Wait for any in-flight job, stop the worker, and return
         *        the context to the calling (main) thread.
         */
        ~RenderThread();

    public:
        /**
         * @brief Post @p renderJob to the worker WITHOUT blocking.
         *
         * Returns immediately. The caller must call waitForFrame() before
         * mutating any data the job depends on (the engine does this at
         * the start of the next frame's mutator phase). Posting another
         * job while one is in flight will block until the in-flight one
         * completes - this is the natural backpressure for the pipeline.
         */
        void postFrame(std::function<void()> renderJob);

        /**
         * @brief Block until the last posted job (if any) has completed.
         *        Safe to call from the main thread at any time; no-op if
         *        no job has ever been posted.
         */
        void waitForFrame();

    private:
        void workerMain();

        GLFWwindow* m_window;
        std::thread m_thread;

        std::mutex                m_mutex;
        std::condition_variable   m_cv;
        std::function<void()>     m_pendingJob;
        bool                      m_jobReady = false;
        bool                      m_jobDone  = true;
        bool                      m_running  = true;
};

} // namespace Engine
