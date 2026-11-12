#pragma once

#include <vector>

#include "asset_editors/asset_editor.h"
#include "core/frame/pose_buffer.h"
#include "preview/preview_viewport.h"

namespace Vkm::Engine {

struct AnimationClipAsset;
struct SkeletonAsset;

/**
 * @brief An animation clip played on the meshes rigged to its skeleton, beside a transport.
 *
 * Posed as the frame poses a rig (composePose), so what plays here is what plays in the scene. A
 * clip that does not fit its rig, or has no mesh rigged to it, says why rather than drawing nothing.
 */
class ClipViewer : public AssetEditor {
    public:
        /**
         * @brief A viewer of the clip at @p key.
         *
         * @param key Its handle's slot and generation.
         */
        explicit ClipViewer(StorageIndex key);
        ~ClipViewer() override = default;

        ClipViewer(const ClipViewer& other) = delete;
        ClipViewer& operator=(const ClipViewer& other) = delete;

        ClipViewer(ClipViewer && other) = delete;
        ClipViewer& operator=(ClipViewer && other) = delete;

    public:
        const Resource* asset(const ResourceManager& resources) const override;
        void            draw(EditorContext& ec) override;

    private:
        /**
         * @brief The playback head, as clipTransport drives a player.
         */
        struct Head {
            float time    = 0.0f;  ///< Seconds.
            float speed   = 1.0f;  ///< Of the clip's own speed.
            bool  playing = true;
            bool  looping = true;
        };

    private:
        /**
         * @brief The meshes to play it on, posed at the playback head.
         *
         * @param ec       The frame's editor context.
         * @param clip     The clip.
         * @param skeleton Its rig, which it fits.
         * @return One draw for each mesh rigged to the skeleton, in the neutral gray.
         */
        std::vector<PreviewDraw> posedDraws(
            EditorContext& ec,
            const AnimationClipAsset& clip,
            const SkeletonAsset& skeleton
        );

        /**
         * @brief The transport and the facts down the side.
         *
         * @param clip     The clip.
         * @param skeleton Its rig, or null when it is not loaded.
         */
        void drawDetails(const AnimationClipAsset& clip, const SkeletonAsset* skeleton);

    private:
        PreviewViewport m_viewport;
        PoseBuffer      m_pose;        ///< The pose the head is at.
        Head            m_head;
        size_t          m_meshes = 0;  ///< The meshes the view drew, for the details beside it.
};

} // namespace Vkm::Engine
