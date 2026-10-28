#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/system.h"

namespace Vkm::Engine {

/**
 * @brief Draws what the scene's components are into DebugDraw, a category at a time while it is
 *        shown: colliders and joints, sound reach, lights, probes, cameras, skeletons, effects and
 *        mesh bounds, and the focus's boxes.
 *
 * The world's half of the editor's gizmos; a game run without the editor, but not a shipped one,
 * shows them with `setShown` on the categories it wants. The focus (DebugDraw::setFocus) draws in
 * the highlight; the camera the frame is drawn through draws no frustum, nor its listener an arrow.
 * Runs in the Render stage before RenderSystem, after the cull it reads the camera and the drawn
 * meshes from.
 */
class DebugViewSystem : public System {
    public:
        DebugViewSystem() = default;
        ~DebugViewSystem() override = default;

        DebugViewSystem(const DebugViewSystem& other) = delete;
        DebugViewSystem& operator=(const DebugViewSystem& other) = delete;

        DebugViewSystem(DebugViewSystem && other) = delete;
        DebugViewSystem& operator=(DebugViewSystem && other) = delete;

    public:
        void update(FrameContext& ctx) override;

    private:
        /// A rig's bones in the world as its skeleton is drawn; a member for its capacity.
        std::vector<glm::mat4> m_bones;
};

} // namespace Vkm::Engine
