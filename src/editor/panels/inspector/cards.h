#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "vkm_audio/device.h"

#include "ecs/entity.h"
#include "ecs/settings/wave_settings.h"
#include "io/scene/scene_components.h"
#include "system/physics/authoring/ragdoll_build.h"

#include "panels/inspector/card_info.h"
#include "ui/rows.h"
#include "ui/file_picker.h"
#include "ui/style/editor_icons.h"
#include "ui/editor_widgets.h"
#include "ui/euler_cache.h"

namespace Vkm::Engine {

class AudioSystem;
struct LiveParticles;
class ResourceManager;
class Scene;
struct EditorState;

/**
 * @brief What the cards keep between frames, for the entity on show.
 */
struct CardMemory {
    EulerCache<EntityId>                                  transformEuler;     ///< Transform's Rotation.
    EulerCache<EntityId>                                  socketEuler;        ///< Bone Socket's Offset
                                                                              ///< rotation.
    std::unordered_map<const glm::quat*, EulerCache<int>> behaviorEulers;     ///< By the quaternion's
                                                                              ///< address.
    std::vector<std::string>                              prefabs;            ///< The project's prefabs,
                                                                              ///< walked as a Prefab field's
                                                                              ///< list opens.
    FilePicker                                            environmentHdr;     ///< The World's Skybox HDR
                                                                              ///< browse.
    Audio::VoiceId                                        auditionVoice = 0;  ///< Remembered so a second
                                                                              ///< press replaces the
                                                                              ///< audition.
    EntityId                                              auditionOwner;      ///< It stops when the selection
                                                                              ///< leaves it.
    RagdollSettings      ragdollSettings{};   ///< For Build.
    EntityId             meshColliderEmpty;   ///< Whose mesh-collider build found no whole triangle.
    int                  colliderFitDetail = 4;  ///< Fit to Mesh's voxel resolution.
    int                  lodLevels         = 2;  ///< LOD levels Generate builds below the source.
    std::optional<WaveShape> waveShape;          ///< The waves the height below was made for.
    float                    waveHeight = 0.0f;  ///< Made again only when their shape changes: costly.
    std::string                            scriptBefore;  ///< The Script card's capture before this frame's
                                                          ///< edits, taken again when scriptKey moves.
    std::optional<std::array<uint64_t, 3>> scriptKey;     ///< The edit, entity and world it was taken at.
};

/**
 * @brief What a card reads beyond its component, as the Inspector hands it each card.
 */
struct CardContext {
    Scene&                scene;
    ResourceManager&      resources;
    EditorState&          state;
    EntityId              id;
    bool                  playing         = false;  ///< A play session holds the scene.
    bool                  gameHasViewport = false;  ///< The game, not the editor, has the viewport.
    float                 simDelta        = 0.0f;   ///< This frame's simulation step; 0 while it stands.
    AudioSystem&          audio;
    const LiveParticles*  particles       = nullptr;
    EntityId              gameEye;                  ///< The camera the game renders, or would start on.
    CardMemory&           memory;
    std::function<void()> after;                    ///< Run once the card's step is pushed: what replaces
                                                    ///< the component, or more than it.
};

// One per scene component, from the list itself: a row without a card does not link.
#define VKM_CARD(Type, Key) void drawCard(Rows<Type>& rows, CardContext& card);
VKM_SCENE_VALUE_COMPONENTS(VKM_CARD, VKM_CARD, VKM_CARD)
#undef VKM_CARD

/**
 * @brief The Script card: its behaviors, each a fold of its fields, one ScriptEditCommand a change.
 *
 * @param card The context; the component is on card.id.
 */
void drawScriptCard(CardContext& card);

/**
 * @brief The World's cards: the scene's Environment and physics settings, while no entity is selected.
 *
 * @param card The context; card.id is empty.
 */
void drawWorldCards(CardContext& card);

} // namespace Vkm::Engine
