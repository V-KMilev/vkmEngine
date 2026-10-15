#pragma once

#include <cstdint>
#include <optional>

#include "resource/asset_ref.h"
#include "resource/asset/audio_clip_asset.h"
#include "system/script/reflected_behavior.h"

#include "lab_state.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief Something a player uses with the Use key: a button, a lever, a launcher.
 *
 * The press is the server's to count (LabTally::count on this entity), so a device beside it, such
 * as a LabRange, acts on the count rising, on every end alike. A walker presses the nearest switch
 * within `reach` (LabWalker); each press clicks where the switch is. The count reaches clients only
 * if the entity is on the wire - authored with a Kinematic body, not a Static one - and onStart
 * reports one that is not.
 */
class LabSwitch : public ReflectedBehavior<LabSwitch> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;

        /// Count a press; the authority's alone, others are told.
        void press();

        /**
         * @brief How many times it has been pressed, as this end was last told.
         *
         * @return The count, or nothing before a client is first told.
         */
        std::optional<uint32_t> presses() const;

    public:
        float reach = 2.5f;  ///< Metres from it a player can use it, across the ground.

        AssetRef<AudioClipAsset> click;  ///< Heard where it is, on each press.

    private:
        CountWatch m_heard;  ///< The presses, to click for each new one
};

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabSwitch)
    VKM_F(reach)
    VKM_F(click)
VKM_REFLECT_END()
