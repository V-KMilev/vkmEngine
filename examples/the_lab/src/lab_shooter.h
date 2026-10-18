#pragma once

#include <deque>
#include <string>

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "resource/asset_ref.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/asset/material_asset.h"
#include "system/script/reflected_behavior.h"

#include "lab_state.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief A character's shot: fired on its player's key, decided by the server, shown by every end.
 *
 * The server judges each shot with every player put back where the shooter saw them
 * (NetSession::Rewind) and records it on the shooter's LabScore: a player is knocked down, a loose
 * prop is pushed. Every end that draws shows each shot from that score - a tracer, sparks, a sound,
 * and on scenery a scorch mark. The shot leaves along the view from the point the character's
 * LabFollowCamera follows; that camera lags a moving body, so then the shot runs a little off the
 * screen's centre.
 */
class LabShooter : public ReflectedBehavior<LabShooter> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;
        void onFixedUpdate(float dt) override;

    public:
        std::string hitEffect = "prefabs/shot_hit.json";  ///< Made where a shot strikes, project-relative.
        std::string tracer    = "prefabs/tracer.json";    ///< Made where a shot leaves, along it.

        AssetRef<MaterialAsset>  scorch;  ///< Left where a shot strikes scenery, as a Decal.
        AssetRef<AudioClipAsset> zap;     ///< Heard where a shot leaves.

    private:
        /// Where a shot leaves: the point the camera follows, without its lag.
        glm::vec3 muzzle();

        /// Fire along the command's view, with every player where this one saw them; the authority's.
        void fire();

        /// Show the shot the score last recorded: a tracer, sparks, a scorch mark.
        void showShot();

        /**
         * @brief Leave a scorch mark where a shot struck scenery, dropping the oldest past the cap.
         *
         * @param at     Where it struck.
         * @param normal The surface there, facing the shooter.
         */
        void leaveScorch(const glm::vec3& at, const glm::vec3& normal);

    private:
        CountWatch           m_shots;     ///< The score's shots, to show each new one
        std::deque<EntityId> m_scorches;  ///< Decals left, oldest first
};

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabShooter)
    VKM_F(hitEffect)
    VKM_F(tracer)
    VKM_F(scorch)
    VKM_F(zap)
VKM_REFLECT_END()
