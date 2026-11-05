#define VKM_LOG_CATEGORY "RENDER"

#include "system/render/screenshot.h"

#include <filesystem>
#include <fstream>
#include <utility>

// A private copy of the writer, so its settings are this file's alone; its initializers trip -Wextra.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma GCC diagnostic pop

#include "vkm_log/logger.h"
#include "vkm_platform/threading/thread_pool.h"

#include "base/profile/profiler.h"

namespace Vkm::Engine {

bool writeScreenshotFile(
    const std::string& path,
    uint32_t width,
    uint32_t height,
    const std::vector<uint8_t>& rgb
) {
    const size_t bytes = static_cast<size_t>(width) * height * 3;
    if (width == 0 || height == 0 || rgb.size() < bytes) return false;

    const std::string extension = std::filesystem::path(path).extension().string();
    if (extension == ".rgb") {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(bytes));
        return static_cast<bool>(out);
    }
    if (extension != ".png") return false;

    stbi_write_png_compression_level = 1;
    stbi_write_force_png_filter      = 0;

    const int stride = static_cast<int>(width) * 3;
    return stbi_write_png(
        path.c_str(),
        static_cast<int>(width),
        static_cast<int>(height),
        3,
        rgb.data(),
        stride
    ) != 0;
}

void ScreenshotWriter::write(std::string path, FramePixels frame) {
    {
        const std::lock_guard<std::mutex> lock(m_queue->mutex);
        m_queue->shots.push_back({std::move(path), std::move(frame)});
        if (m_queue->draining) return;
        m_queue->draining = true;
    }
    // One task at a time, so the writes keep their order; it holds the queue, not this.
    Platform::ThreadPool::get().addTask([queue = m_queue] { drain(*queue); });
}

void ScreenshotWriter::finish() {
    drain(*m_queue);
}

void ScreenshotWriter::drain(Queue& queue) {
    for (;;) {
        Shot shot;
        {
            const std::lock_guard<std::mutex> lock(queue.mutex);
            if (queue.shots.empty()) {
                queue.draining = false;
                return;
            }
            shot = std::move(queue.shots.front());
            queue.shots.pop_front();
        }
        PROFILE_SCOPE("Screenshot/Write");
        const FramePixels& frame = shot.frame;
        if (writeScreenshotFile(shot.path, frame.width, frame.height, frame.rgb)) {
            LOG_INFO("Screenshot written to %s", shot.path.c_str());
        } else {
            LOG_WARNING(
                "Screenshot could not be written to %s: name a .png or a .rgb, in a folder that exists",
                shot.path.c_str()
            );
        }
    }
}

} // namespace Vkm::Engine
