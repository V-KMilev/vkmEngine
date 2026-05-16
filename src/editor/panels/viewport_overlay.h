#pragma once

#include <imgui.h>

#include "platform/system_metrics.h"

namespace Engine {

struct EditorContext;

/**
 * @brief Viewport overlay displaying performance stats, gizmo mode indicator, and navigation gizmo.
 *
 * Owns frame time history (240-frame ring buffer) and system metrics (CPU/RAM/GPU/VRAM).
 * Drawn inside the viewport child window on top of the 3D scene.
 */
class ViewportOverlay {
    public:
        void draw(EditorContext& ec);
        void drawNavigationGizmo(EditorContext& ec);
        void updateMetrics(float deltaTime);

        /// Push one frame's render time (ms) into the ring buffer.
        void pushFrameTime(float ms);

    private:
        static constexpr int FRAME_HISTORY_SIZE = 240;
        float m_frameTimeHistory[FRAME_HISTORY_SIZE] = {};
        int   m_frameTimeOffset = 0;
        float m_frameTimeMax    = 0.0f;

        SystemMetrics m_metrics;
};

} // namespace Engine
