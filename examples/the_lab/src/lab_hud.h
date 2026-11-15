#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "system/script/behavior.h"

#include "lab_state.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief The player's screen: a crosshair, the tokens they have found, the area they are in, and a
 *        map of the lab on M.
 *
 * Reads the LabScore on the character this player drives (localCharacter), the LabArea markers
 * for the map and the LabToken count. A banner names an area as the player walks into it, and
 * cheers a token found. Built on every end that draws; hidden while the game is paused, under the
 * pause menu.
 */
class LabHud : public Behavior<LabHud> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;
        void onRealtimeUpdate(float dt) override;

    public:
        float bannerSeconds = 3.5f;

    private:
        /**
         * @brief An area as the map and the banners use it.
         */
        struct Area {
            EntityId    entity;
            std::string title;
            std::string blurb;
            glm::vec2   center{0.0f};
            glm::vec2   size{0.0f};
        };

        /**
         * @brief A character and its dot on the map.
         */
        struct Pin {
            EntityId character;
            EntityId dot;
        };

    private:
        /// Lay the map out: a patch per area, and a dot per character.
        void buildMap();

        /// Move the map's dots to where the characters are.
        void updateMap();

        /**
         * @brief Put a banner up across the top of the screen.
         *
         * @param title Large.
         * @param blurb Under it.
         */
        void showBanner(const std::string& title, const std::string& blurb);

        /**
         * @brief Name the area under @p feet as the player walks into it.
         *
         * @param feet Where the player stands, across the ground.
         */
        void nameArea(const glm::vec2& feet);

        /**
         * @brief Cheer each token the player finds.
         *
         * @param tokens The player's LabScore::tokens.
         */
        void cheerTokens(uint32_t tokens);

        /// Where @p world lands on the map panel, in its pixels from its top left.
        glm::vec2 onMap(const glm::vec2& world) const;

    private:
        std::vector<Area> m_areas;
        std::vector<Pin>  m_pins;  ///< One per authored character

        EntityId m_canvas{};
        EntityId m_score{};
        EntityId m_banner{};
        EntityId m_bannerBlurb{};
        EntityId m_hint{};
        EntityId m_map{};

        glm::vec2 m_worldMin{0.0f};
        float     m_mapScale = 1.0f;  ///< Map pixels a meter
        glm::vec2 m_mapOrigin{0.0f};  ///< Where m_worldMin lands, in map pixels

        uint32_t   m_everyToken = 0;     ///< Every token's bit in the scene
        CountWatch m_tokens;             ///< The player's tokens, to cheer each new one
        int        m_inArea     = -1;
        float      m_bannerLeft = 0.0f;
};

} // namespace Lab

VKM_BEHAVIOR(::Lab::LabHud)
    VKM_F(bannerSeconds, "How long a banner stays up.")
VKM_REFLECT_END()
