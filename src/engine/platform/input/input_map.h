#pragma once

#include <cstdint>
#include <bitset>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json_fwd.hpp>

#include "core/reflect.h"

#include "platform/input/input_command.h"
#include "platform/input/input_handle.h"

namespace Vkm::Engine {

class HostChrome;

/**
 * @brief Action names the engine itself reads.
 *
 * Strings, not an enum, so binding files can name actions the engine never heard of.
 */
namespace InputActions {
    /**
     * @brief Press, hold and release of the game UI's primary pointer button.
     *
     * An action, not a raw button, so it is rebindable. InputMap binds it to the left mouse
     * button at construction and after reset().
     */
    inline constexpr const char* UI_CLICK = "UI/Click";

} // namespace InputActions

/**
 * @brief What physically produces an action's value.
 *
 * A new device (gamepad) is a new value here and a case in each switch in InputMap::update.
 */
enum class InputSource : uint8_t {
    Key,
    MouseButton,
    Count  ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

/**
 * @brief One physical control contributing to an action.
 *
 * `scale` makes an action an axis: W at +1 and S at -1 make "Move/Forward" read -1..1.
 */
struct InputBinding {
    InputSource source = InputSource::Key;
    int         code   = 0;     ///< GLFW key or mouse-button code.
    float       scale  = 1.0f;  ///< Contribution to axis(); sign gives direction.
};

/**
 * @brief What a binding is called, for a controls screen: "W", "Left Shift", "Left mouse".
 *
 * In English and by position, as the codes are: the key a US layout prints there.
 *
 * @param binding The key or button.
 * @return Its name; "Key N" or "Mouse N" for one with no name of its own.
 */
std::string bindingName(const InputBinding& binding);

/**
 * @brief Named actions resolved from physical bindings, sampled once per frame.
 *
 * Sampled before any system runs, so every reader in the frame agrees. The frame queries serve
 * frame-rate work only: a fixed update reads the per-tick InputCommand that beginTick() builds
 * from the edges latched between ticks.
 */
class InputMap {
    public:
        InputMap();
        ~InputMap() = default;

        InputMap(const InputMap& other) = delete;
        InputMap& operator=(const InputMap& other) = delete;

        InputMap(InputMap && other) = delete;
        InputMap& operator=(InputMap && other) = delete;

    public:
        /**
         * @brief Define (or replace) an action and its default bindings.
         *
         * A binding the player saved for it (loadBindings) is put in their place.
         *
         * @param action   Action name, e.g. "Move/Forward"; the stable identity a saved
         *                 binding file refers to.
         * @param bindings Physical controls feeding it by default.
         */
        void define(std::string_view action, std::vector<InputBinding> bindings);

        /**
         * @brief The player's own bindings for @p action, its defaults kept; what a controls
         *        screen sets. Defines an action that was not.
         *
         * @param action   Action name.
         * @param bindings Physical controls feeding it now.
         */
        void rebind(std::string_view action, std::vector<InputBinding> bindings);

        /**
         * @brief Every action back to its defaults, the player's own bindings forgotten.
         */
        void resetBindings();

        /**
         * @brief The actions the player rebound, each its bindings, for a settings file.
         *
         * Only those that differ from their defaults: one left alone follows its default when the
         * game changes it.
         *
         * @return An object, an action name a key, each an array of bindings.
         */
        nlohmann::json saveBindings() const;

        /**
         * @brief Rebinds each action @p saved names, as saveBindings wrote it.
         *
         * Kept as the player's, as rebind's are, through reset(): one not defined yet, or
         * defined again, takes them when it is. An entry that cannot be read is skipped.
         *
         * @param saved An object as saveBindings writes; anything else changes nothing.
         */
        void loadBindings(const nlohmann::json& saved);

        /**
         * @brief Bindings currently feeding @p action.
         *
         * @param action Action name.
         * @return Its bindings; empty if the action is undefined.
         */
        const std::vector<InputBinding>& bindings(std::string_view action) const;

        /**
         * @brief Every defined action name, in name order.
         *
         * @return The names, sorted, so two runs produce the same file.
         */
        std::vector<std::string> actions() const;

        /**
         * @brief Sample every action from @p input, rolling the previous values.
         *
         * Called once per frame before the systems run; also latches edges for the next command.
         * A binding struck since the last sample reads as down, so a sub-frame tap is held for
         * one frame. A device the host's UI holds reads as untouched (the pointer position still
         * travels). A press belongs to whoever held the device when it began, until let go; a
         * press beginning over a blocking game UI element reads as down for UI_CLICK alone.
         *
         * @param input  Device state to sample; its strikes are cleared.
         * @param chrome What the host's UI holds this frame.
         */
        void update(InputHandle& input, const HostChrome& chrome);

        /**
         * @brief Say whether the pointer is over a blocking element of the game's UI.
         *
         * Read by the next update(): one frame of lag, as the host's chrome has.
         *
         * @param over Whether a blocking element is under the pointer.
         */
        void setPointerOverUI(bool over) { m_pointerOverUI = over; }

        /**
         * @brief Drop the edges latched since the last command.
         *
         * For a frame no tick follows (Clock::isSimulationStill); otherwise the next tick, such as
         * the first of a Play session, would get every press made meanwhile.
         */
        void discardPendingEdges();

        /**
         * @brief Forget every action and every latched edge, then define the engine's own.
         *
         * Frees the command slots, which kept would fill MAX_INPUT_ACTIONS across sessions. The
         * command sequence keeps counting, since an ack names it; a press held across a reset
         * stays with its holder until let go.
         */
        void reset();

        /**
         * @brief Build the command for @p tick from everything since the last one.
         *
         * Called once per fixed step before the systems run. Clears the latch, so one keypress
         * does not read as pressed on every tick of a slow frame.
         *
         * @param tick Simulation tick the command drives.
         */
        void beginTick(uint32_t tick);

        /**
         * @brief Say where the player is looking, for the commands that follow.
         *
         * Read back off InputCommand::view in a fixed update. A game steering relative to its
         * camera calls this once per frame after moving it.
         *
         * @param view Orientation the input from now on is aimed with.
         */
        void setView(const glm::quat& view) { m_view = view; }

        /**
         * @brief Say what the game sends beside the actions, for the commands that follow.
         *
         * Held, as the view is: every command carries it until it changes - state beside the
         * actions, such as where a player is looking. What must arrive once is a message
         * (NetSession::sendMessage). Read back off InputCommand::payload; zero bytes clears it.
         *
         * @param bytes What to send.
         * @param size  How many, up to MAX_COMMAND_PAYLOAD.
         * @return False, keeping what was held, when there are more than a command carries.
         */
        bool setPayload(const uint8_t* bytes, size_t size);

        /**
         * @brief The command's slot for @p action, or -1 when it has none.
         *
         * Assigned in definition order, stable for the session. An action past MAX_INPUT_ACTIONS
         * has no slot: absent from every command, still answered by the frame queries.
         *
         * @param action Action name.
         * @return Index into InputCommand::axis, or -1.
         */
        int indexOf(std::string_view action) const;

        /**
         * @brief A fingerprint of which action holds which command slot.
         *
         * Two ends defining the same actions in a different order agree on actionCount() but
         * misread each other's commands; this tells them apart.
         *
         * @return A hash of the slotted names in slot order, or zero when no
         *         action has a slot.
         */
        uint64_t actionFingerprint() const;

        /**
         * @brief Is the action active this frame?
         *
         * The summed value decides: W and S held together cancel.
         *
         * @param action Action name.
         * @return Whether its value is at or past the activation threshold; false if undefined.
         */
        bool held(std::string_view action) const;

        /**
         * @brief Did it become active this frame?
         *
         * @param action Action name.
         * @return Whether it is active now and was not the previous frame.
         */
        bool pressed(std::string_view action) const;

        /**
         * @brief Did it stop being active this frame?
         *
         * @param action Action name.
         * @return Whether it was active the previous frame and is not now.
         */
        bool released(std::string_view action) const;

        /**
         * @brief Summed, clamped value of the action's bindings, -1..1.
         *
         * @param action Action name.
         * @return Its value this frame; 0 if undefined.
         */
        float axis(std::string_view action) const;

        /**
         * @brief Is the action active in @p command? (the tick's answer)
         *
         * A fixed update must read these command queries: the frame queries drop a tap between
         * ticks and repeat a press on every tick of a slow frame. Only the name-to-slot resolution
         * comes from this map; an action with no slot reads as inactive.
         *
         * @param command Command to read; the tick's, a peer's, or a replayed one.
         * @param action Action name.
         * @return Whether the action reads as down in that command.
         */
        bool held(const InputCommand& command, std::string_view action) const;

        /**
         * @brief Did it become active since the previous command?
         *
         * @param command Command to read.
         * @param action Action name.
         * @return Whether the command carries a press edge for it.
         */
        bool pressed(const InputCommand& command, std::string_view action) const;

        /**
         * @brief Did it stop being active since the previous command?
         *
         * @param command Command to read.
         * @param action Action name.
         * @return Whether the command carries a release edge for it.
         */
        bool released(const InputCommand& command, std::string_view action) const;

        /**
         * @brief The action's value in @p command, -1..1.
         *
         * @param command Command to read.
         * @param action Action name.
         * @return Its axis value, or 0 when the action has no command slot.
         */
        float axis(const InputCommand& command, std::string_view action) const;

        /**
         * @brief Whether the pointer was over a blocking element of the game's UI.
         *
         * As of the last UI layout; e.g. for hiding a crosshair over a panel.
         *
         * @return What UISystem last set through setPointerOverUI.
         */
        bool pointerOverUI() const { return m_pointerOverUI; }

        /**
         * @brief Where the pointer is this frame, in window pixels.
         *
         * Not framebuffer pixels: scale by WindowManager::framebufferScale for those.
         *
         * @return Cursor position, origin at the window's top-left.
         */
        const glm::vec2& pointer() const { return m_pointer; }

        /**
         * @brief How far the pointer moved since the previous frame, in window pixels.
         *
         * Zero on the first frame and while no window exists.
         *
         * @return The movement; zero while the host's UI holds the pointer.
         */
        const glm::vec2& pointerDelta() const { return m_pointerDelta; }

        /**
         * @brief Wheel notches turned this frame for gameplay, positive away from the viewer.
         *
         * Zero while the pointer is over a blocking game UI element or the host's UI holds it.
         *
         * @return The notches gameplay may act on.
         */
        float wheel() const { return m_wheel; }

        /**
         * @brief Wheel notches turned this frame, whatever is under the pointer.
         *
         * Zero only while the host's UI holds the pointer.
         *
         * @return Every notch turned.
         */
        float uiWheel() const { return m_uiWheel; }

        /**
         * @brief The characters typed this frame, in order, as UTF-32.
         *
         * Layout, Shift and dead keys applied, which no binding can say. Empty while the host's
         * UI holds the keyboard.
         *
         * @return This frame's characters, valid until the next update().
         */
        std::u32string_view text() const { return m_text; }

        /**
         * @brief Whether @p key went down or auto-repeated this frame.
         *
         * For editing keys (Backspace, arrows, Enter), which act again at the repeat rate as
         * pressed() does not. False while the host's UI holds the keyboard.
         *
         * @param key GLFW key code.
         * @return Whether it was typed this frame; false for out-of-range keys.
         */
        bool typed(int key) const { return key >= 0 && key <= MAX_KEY && m_typed[static_cast<size_t>(key)]; }

        /**
         * @brief The key or mouse button that went down this frame, bound or not.
         *
         * What a rebinding screen listens for. Keys before buttons, the lowest code first, when
         * several went down together; a device the host's UI holds says nothing.
         *
         * @return The press, as a binding of scale one; nothing when there was none.
         */
        const std::optional<InputBinding>& struck() const { return m_struck; }

        /**
         * @brief The command the current fixed step is running under.
         *
         * Zeroed before the first beginTick(), so an early reader sees no input, not stale input.
         *
         * @return The current tick's command.
         */
        const InputCommand& command() const { return m_command; }

        /**
         * @brief How many action slots are in use.
         *
         * The width of the part of a command that carries anything; both ends of a session
         * must agree on it.
         *
         * @return Slots assigned, at most MAX_INPUT_ACTIONS.
         */
        uint32_t actionCount() const { return m_nextSlot; }

    private:
        struct Action {
            std::vector<InputBinding> bindings;
            std::vector<InputBinding> defaults;  ///< As defined; what a rebinding is measured from.

            float value     = 0.0f;   ///< This frame's axis value.
            bool  active    = false;  ///< |value| past the threshold.
            bool  wasActive = false;  ///< Previous frame's, for the edges.
            int   slot      = -1;     ///< Index in an InputCommand, or -1 past the cap.
        };

        /**
         * @brief Who a press belongs to, decided on the frame it begins.
         */
        enum class Holder : uint8_t {
            Host,      ///< Reads as up for every action.
            UI,        ///< Reads as down for UI_CLICK alone.
            Gameplay,  ///< Reads as down for every action.
        };

        /**
         * @brief A key or button that is down, and whose press it is.
         */
        struct HeldPress {
            InputSource source;
            int         code;
            Holder      holder;
        };

        /**
         * @brief Define the actions in InputActions with their default bindings.
         *
         * Before any project's, so a saved binding file overrides them and they hold the first slots.
         */
        void defineEngineActions();

        /**
         * @brief Look up an action, or null when it was never defined.
         *
         * @param action Action name.
         * @return Its entry, or nullptr.
         */
        const Action* find(std::string_view action) const;

        /**
         * @brief The entry for @p action, created empty if it is new.
         *
         * The one place a name becomes a stored std::string, keeping queries allocation-free.
         *
         * @param action Action name.
         * @return Its entry.
         */
        Action& mutableAction(std::string_view action);

        /**
         * @brief Give @p entry a command slot if it has none and one is left.
         *
         * @param entry Action being defined or extended.
         * @param action Its name, for the warning at the cap.
         */
        void assignSlot(Action& entry, std::string_view action);

        /**
         * @brief Whether @p binding reads as up for the action being sampled,
         *        recording whose press it is on the frame it begins.
         *
         * Later frames read only that record, so pointer movement changes nothing; only the host
         * taking the device makes the press the host's until let go.
         *
         * @param binding  The binding being sampled.
         * @param down     Whether its key or button is down.
         * @param hostHas  Whether the host's chrome holds that device this frame.
         * @param uiHas    Whether the game's UI would take a press of it now.
         * @param forUI    Whether the action being sampled is UI_CLICK.
         * @return True when the binding must read as up for this action.
         */
        bool heldElsewhere(const InputBinding& binding, bool down, bool hostHas, bool uiHas, bool forUI);

    private:
        /**
         * @brief Every defined action, by name.
         *
         * Transparent comparator: a string_view lookup builds no std::string.
         */
        std::map<std::string, Action, std::less<>> m_actions;
        /// The player's own bindings, by action, rebound or loaded: define() puts them in place.
        std::map<std::string, std::vector<InputBinding>, std::less<>> m_saved;

        glm::vec2 m_pointer{0.0f};       ///< Window pixels.
        glm::vec2 m_pointerDelta{0.0f};
        float     m_wheel   = 0.0f;      ///< Notches gameplay may act on.
        float     m_uiWheel = 0.0f;      ///< Every notch.

        std::u32string             m_text;   ///< Capacity reused.
        std::bitset<MAX_KEY + 1>   m_typed;  ///< Keys pressed or repeated this frame.
        std::optional<InputBinding> m_struck;  ///< The press this frame, for a rebinding screen.

        InputCommand m_command;              ///< The current tick's.
        glm::quat    m_view{1.0f, 0.0f, 0.0f, 0.0f};  ///< Latched into every command beginTick builds.
        std::array<uint8_t, MAX_COMMAND_PAYLOAD> m_payload{};  ///< Latched too, payloadSize of it.
        uint8_t      m_payloadSize = 0;
        uint32_t     m_pendingPressed  = 0;  ///< Edges latched since the last command.
        uint32_t     m_pendingReleased = 0;
        uint32_t     m_nextSequence    = 1;  ///< 0 is the zeroed pre-first-tick command.
        uint32_t     m_nextSlot        = 0;

        bool m_pointerOverUI = false;  ///< From the last UI layout.

        /**
         * @brief Every key and button down, with whose press it is, until released.
         *
         * Few are held at once, so a scan beats a set.
         */
        std::vector<HeldPress> m_heldPresses;
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::InputSource, "Key", "MouseButton")
