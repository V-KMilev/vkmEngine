#include "system/async/async_load_queue.h"

namespace Engine {

AsyncLoadQueue& AsyncLoadQueue::get() {
    static AsyncLoadQueue instance;
    return instance;
}

void AsyncLoadQueue::pushTexture(TextureLoadCompletion completion) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_textures.push_back(std::move(completion));
}

std::vector<TextureLoadCompletion> AsyncLoadQueue::drainTextures() {
    std::vector<TextureLoadCompletion> out;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        out.swap(m_textures);
    }
    return out;
}

} // namespace Engine
