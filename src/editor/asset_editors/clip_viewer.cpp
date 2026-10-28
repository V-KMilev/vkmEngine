#include "asset_editors/clip_viewer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <imgui.h>

#include "asset_editors/asset_editors.h"
#include "core/clock.h"
#include "core/fnv1a.h"
#include "core/system.h"
#include "editor_context.h"
#include "panels/material_editor_panel.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/resource_manager.h"
#include "system/animation/pose_evaluator.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

namespace {

/// The slowest it plays, of the clip's own speed.
constexpr float SLOWEST = 0.05f;

/**
 * @brief Why nothing plays, in the middle of the space a viewport would take.
 *
 * @param why The reason.
 */
void drawNothingPlays(const char* why) {
    const ImVec2 room  = ImGui::GetContentRegionAvail();
    const ImVec2 words = ImGui::CalcTextSize(why);
    const float x = ImGui::GetCursorPosX() + std::max(0.0f, (room.x - words.x) * 0.5f);
    const float y = ImGui::GetCursorPosY() + std::max(0.0f, (room.y - words.y) * 0.5f);
    ImGui::SetCursorPos(ImVec2(x, y));
    ImGui::TextDisabled("%s", why);
}

} // namespace

ClipViewer::ClipViewer(StorageIndex key)
    : AssetEditor(AssetType::AnimationClip, key)
    , m_viewport(AssetEditors::viewportKey(AssetType::AnimationClip, key))
{}

const Resource* ClipViewer::asset(const ResourceManager& resources) const {
    return resources.tryGet(AnimationClipHandle{key()});
}

std::vector<PreviewDraw> ClipViewer::posedDraws(
    EditorContext& ec,
    const AnimationClipAsset& clip,
    const SkeletonAsset& skeleton
) {
    ResourceManager& resources = ec.frame.resources;

    // Posed as the frame poses a rig: one slice, the clip at the head.
    m_pose.clear();
    const uint32_t slice = m_pose.addSlice(static_cast<uint32_t>(skeleton.bones.size()));
    PoseSample sample;
    sample.clip = &clip;
    sample.time = m_head.time;
    composePose(skeleton, sample, m_pose.writeTo(slice));
    const PoseSlice& posed = m_pose.slices()[slice];
    const auto       from  = m_pose.palette().begin() + posed.first;
    const std::vector<glm::mat4> bones(from, from + posed.count);

    std::vector<PreviewDraw> draws;
    const MaterialHandle grey = neutralPreviewMaterial(resources);
    resources.forEachOfType<MeshAsset>([&](MeshHandle mesh, const MeshAsset& asset) {
        if (asset.isHidden() || asset.skin.empty() || asset.skeleton != skeleton.name()) return;
        draws.push_back({mesh, grey, glm::mat4(1.0f), bones});
    });
    return draws;
}

void ClipViewer::draw(EditorContext& ec) {
    ResourceManager&          resources = ec.frame.resources;
    const AnimationClipAsset& clip      = resources.get(AnimationClipHandle{key()});
    const SkeletonHandle      rig       = resources.findByName<SkeletonAsset>(clip.skeleton);
    const SkeletonAsset*      skeleton  = resources.tryGet(rig);

    // Why it cannot play, if it cannot.
    const char* nothing = nullptr;
    if (!skeleton) {
        nothing = "Its skeleton is not loaded";
    } else if (!findSkeletonFault(*skeleton).empty()) {
        nothing = "Its skeleton cannot be posed";
    } else if (!clipFitsRig(clip, *skeleton) || !findClipFault(clip).empty()) {
        nothing = "It does not fit its skeleton";
    }

    if (m_head.playing && !nothing) {
        const float step = ec.frame.clock.getDeltaTime() * m_head.speed;
        if (!advanceHead(m_head.time, clip.duration, step, m_head.looping)) m_head.playing = false;
    }

    const auto view = [&] {
        std::vector<PreviewDraw> draws;
        if (!nothing) {
            draws = posedDraws(ec, clip, *skeleton);
            if (draws.empty()) nothing = "No mesh is rigged to its skeleton";
        }
        m_meshes = draws.size();
        if (nothing) {
            drawNothingPlays(nothing);
        } else {
            // The head is part of the picture, as the meshes are.
            uint64_t version = fnv1a64Bytes(&m_head.time, sizeof(m_head.time), clip.version());
            for (const PreviewDraw& draw : draws) {
                const uint64_t meshVersion = resources.get(draw.mesh).version();
                version = fnv1a64Bytes(&meshVersion, sizeof(meshVersion), version);
            }
            m_viewport.draw(ec, draws, version, glm::vec2(0.0f));
        }
    };
    viewAndDetails(view, [&] { drawDetails(clip, skeleton); });
}

void ClipViewer::drawDetails(const AnimationClipAsset& clip, const SkeletonAsset* skeleton) {
    sectionLabel("Play");
    clipTransport("clip", m_head, SLOWEST, -1.0f);
    // A one-shot that has run out starts over when played.
    if (m_head.playing && !m_head.looping && m_head.time >= clip.duration) m_head.time = 0.0f;
    drawPropertyLabel("Time");
    ImGui::SetNextItemWidth(-1.0f);
    // Scrubbed, it holds where it is put.
    const float length = std::max(clip.duration, 0.0f);
    if (sliderFloat("##time", &m_head.time, 0.0f, length, "%.2f s")) m_head.playing = false;

    ImGui::Spacing();
    sectionLabel("Clip");
    char line[96];
    std::snprintf(line, sizeof(line), "%.2f s", static_cast<double>(clip.duration));
    factRow("Length", line);
    factRow("Skeleton", clip.skeleton.empty() ? "(none)" : clip.skeleton.c_str());
    std::snprintf(line, sizeof(line), "%zu", skeleton ? skeleton->bones.size() : clip.bones.size());
    factRow("Bones", line);
    std::snprintf(line, sizeof(line), "%zu", clip.markers.size());
    factRow("Markers", line);
    std::snprintf(line, sizeof(line), "%zu", m_meshes);
    factRow("Played on", line);
    ImGui::SetItemTooltip("The meshes rigged to its skeleton, each shown in a plain grey");
}

} // namespace Vkm::Engine
