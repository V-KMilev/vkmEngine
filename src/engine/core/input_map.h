#pragma once

#include <cstdint>
#include <bitset>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json_fwd.hpp>

#include "base/hash/fnv1a.h"

#include "platform/input/input_binding.h"
#include "platform/input/input_command.h"
#include "vkm_platform/input/input_handle.h"

namespace Vkm::Engine {

struct HostChrome;

/**
 * @brief Action names the engine itself reads.
 *
 * Strings, not an enum, so binding files can name actions the engine never heard of.
 */
namespace InputActions {
    inline constexpr const char* UI_CLICK = "UI/Click";  ///< Press, hold and release of the game UI's primary
                                                         ///< pointer button: an action, not a raw button, so
                                                         ///< it is rebindable. InputMap binds it to the left
                                                         ///< mouse button at construction and after reset();
                                                         ///< a game's UI/Click in project.json's actions
                                                         ///< replaces it.

} // namespace InputActions

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
         * @brief The game's actions, the whole set, in place of the last: what project.json lists.
         *
         * Their command slots follow the engine's own in name order, so two ends listing the same
         * actions agree whatever order they list them in. A binding the player saved is put in
         * place of an action's own, and reset() keeps them.
         *
         * @param actions Each action with its default bindings.
         */
        void setActions(std::vector<InputAction> actions);

        /**
         * @brief The player's own bindings for @p action, its defaults kept; what a controls
         *        screen sets. Written to the player's file (keepBindingsIn).
         *
         * @param action   An action setActions named; any other is refused with a warning.
         * @param bindings Physical controls feeding it now.
         */
        void rebind(std::string_view action, std::vector<InputBinding> bindings);

        /**
         * @brief Every action back to its defaults, the player's own bindings forgotten.
         */
        void resetBindings();

        /**
         * @brief Keep the player's bindings in @p file: read now, and written as they change.
         *
         * @param file The player's file, in Roots::playerRoot.
         */
        void keepBindingsIn(std::filesystem::path file);

        /**
         * @brief What to press for @p action, for a hint on screen: its first binding's name.
         *
         * @param action Action name.
         * @return bindingName of its first binding; empty when it has none.
         */
        std::string keyName(std::string_view action) const;

        /**
         * @brief The actions the player rebound, each its bindings, for a settings file.
         *
         * Only those that differ from their defaults: one left alone follows its default when the
         * game changes it.
         *
         * @return An object, an action name a key, each an array of bindings as project.json
         *         writes them.
         */
        nlohmann::json saveBindings() const;

        /**
         * @brief Rebinds each action @p saved names, as saveBindings wrote it.
         *
         * Kept as the player's, as rebind's are, through reset(): one not listed yet takes them
         * when setActions lists it. An entry that cannot be read is skipped with a warning.
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
        void update(Platform::InputHandle& input, const HostChrome& chrome);

        /**
         * @brief Give where the game's UI blocks the pointer, as last laid out, in window pixels.
         *
         * The pointer is over the UI (pointerOverUI) while it is inside one, asked now and at each
         * update() of where it is then, not where the layout saw it: a click the frame the pointer
         * crosses a panel's edge goes to whoever is under it.
         *
         * @param areas Each blocking element's visible rect: x, y, width, height.
         */
        void setPointerBlockers(const std::vector<glm::vec4>& areas) {
            m_blockers.assign(areas.begin(), areas.end());
            m_blockersKnown = true;
            m_pointerOverUI = pointerInBlockers();
        }

        /**
         * @brief Drop the edges latched since the last command.
         *
         * For a frame no tick follows (Clock::isSimulationStill); otherwise the next tick, such as
         * the first of a Play session, would get every press made meanwhile.
         */
        void discardPendingEdges();

        /**
         * @brief Forget every latched edge and value, then define the engine's actions and the
         *        game's again.
         *
         * The command sequence keeps counting, since an ack names it; a press held across a reset
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
         * Engine::run says it every frame, before the ticks, with the active camera's view as the
         * last frame showed it; a tick reads it back off InputCommand::view (BehaviorBase::alongView).
         *
         * @param view Orientation the input from now on is aimed with.
         */
        void setView(const glm::quat& view) { m_view = view; }

        /**
         * @brief Say what the game sends beside the actions, for the commands that follow.
         *
         * Held, as the view is: every command carries it until it changes - state beside the
         * actions, such as which item a player's cursor is over. What must arrive once is a message
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
         * The engine's actions first, then the game's in name order. An action past
         * MAX_INPUT_ACTIONS has no slot: absent from every command, still answered by the frame
         * queries.
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
         * @return A hash of the slotted names in slot order.
         */
        uint64_t actionFingerprint() const { return m_fingerprint; }

        /**
         * @brief Is the action active this frame?
         *
         * The summed value decides: W and S held together cancel. An action read but never listed
         * warns once, by name, and reads as never pressed, here and in every query.
         *
         * @param action Action name.
         * @return Whether its value reaches 0.5 either way; false if undefined.
         */
        bool held(std::string_view action) const;

        /**
         * @brief Did it become active this frame?
         *
         * Active as held() reads it: a summed value of 0.5 or more either way.
         *
         * @param action Action name.
         * @return Whether it is active now and was not the previous frame, or was let go and
         *         pressed again since.
         */
        bool pressed(std::string_view action) const;

        /**
         * @brief Did it stop being active this frame?
         *
         * Active as held() reads it: a summed value of 0.5 or more either way.
         *
         * @param action Action name.
         * @return Whether it was active the previous frame and is not now, or was let go and
         *         pressed again since.
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
         * Inside an area setPointerBlockers gave; e.g. for hiding a crosshair over a panel.
         *
         * @return Whether it was, at the last update() or blockers given.
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
         * UI holds the keyboard. The keys still reach gameplay bound to them, so a text field
         * belongs where the game is not listening: a menu, a pause screen.
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
         * @param key The key to ask about.
         * @return Whether it was typed this frame.
         */
        bool typed(Platform::Key key) const { return m_typed[static_cast<size_t>(key)]; }

        /**
         * @brief The key or mouse button that went down this frame, bound or not.
         *
         * What a rebinding screen listens for. Keys before buttons, each in its enum's order, when
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
        static constexpr size_t KEYS    = static_cast<size_t>(Platform::Key::Count);
        static constexpr size_t BUTTONS = static_cast<size_t>(Platform::MouseButton::Count);

        struct Action {
            std::vector<InputBinding> bindings;
            std::vector<InputBinding> defaults;  ///< As defined; what a rebinding is measured from.

            float value     = 0.0f;   ///< This frame's axis value.
            bool  active    = false;  ///< |value| past the threshold.
            bool  wasActive = false;  ///< Previous frame's, for the edges.
            bool  restruck  = false;  ///< Let go and pressed again between the two.
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
            InputControl control;
            Holder       holder;
        };

        /**
         * @brief Define (or replace) an action and its default bindings, the player's own put in
         *        their place.
         *
         * @param action   Action name.
         * @param bindings Physical controls feeding it by default.
         */
        void define(std::string_view action, std::vector<InputBinding> bindings);

        /**
         * @brief Empty the action table, then define the actions in InputActions and the game's
         *        (setActions), in that order, so slots and the fingerprint start afresh.
         */
        void rebuildActions();

        /**
         * @brief The action a query names, or null, said once a name when the game never listed it.
         *
         * @param action Action name.
         * @return Its entry, or nullptr.
         */
        const Action* queried(std::string_view action) const;

        /// Write the player's bindings to the kept file, if there is one.
        void writeBindings() const;

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

        /// Whether the pointer, where update() last saw it, is inside one of m_blockers.
        bool pointerInBlockers() const;

    private:
        std::map<std::string, Action, std::less<>>                    m_actions;       ///< Every defined
                                                                                       ///< action, by name.
                                                                                       ///< Transparent
                                                                                       ///< comparator: a
                                                                                       ///< string_view lookup
                                                                                       ///< builds no
                                                                                       ///< std::string.
        std::map<std::string, std::vector<InputBinding>, std::less<>> m_saved;         ///< The player's own
                                                                                       ///< bindings, by
                                                                                       ///< action, rebound or
                                                                                       ///< loaded: define()
                                                                                       ///< puts them in
                                                                                       ///< place.
        std::vector<InputAction>                                      m_gameActions;   ///< setActions', in
                                                                                       ///< name order.
        std::filesystem::path                                         m_bindingsFile;  ///< Where the player's
                                                                                       ///< bindings are kept;
                                                                                       ///< or none.

        mutable std::set<std::string, std::less<>> m_unknownRead;
            ///< Names read that no action has, each warned of once.

        glm::vec2 m_pointer{0.0f};       ///< Window pixels.
        glm::vec2 m_pointerDelta{0.0f};
        float     m_wheel   = 0.0f;      ///< Notches gameplay may act on.
        float     m_uiWheel = 0.0f;      ///< Every notch.

        std::u32string              m_text;    ///< Capacity reused.
        std::bitset<KEYS>           m_typed;   ///< Keys pressed or repeated this frame.
        std::optional<InputBinding> m_struck;  ///< The press this frame, for a rebinding screen.

        InputCommand m_command;              ///< The current tick's.
        glm::quat    m_view{1.0f, 0.0f, 0.0f, 0.0f};  ///< Latched into every command beginTick builds.
        std::array<uint8_t, MAX_COMMAND_PAYLOAD> m_payload{};  ///< Latched too, payloadSize of it.
        uint8_t      m_payloadSize = 0;
        uint32_t     m_pendingPressed  = 0;  ///< Edges latched since the last command.
        uint32_t     m_pendingReleased = 0;
        uint32_t     m_nextSequence    = 1;  ///< 0 is the zeroed pre-first-tick command.
        uint32_t     m_nextSlot        = 0;
        uint64_t     m_fingerprint     = FNV1A_OFFSET_BASIS;  ///< See actionFingerprint.

        bool                   m_pointerOverUI = false;  ///< See pointerOverUI.
        std::vector<glm::vec4> m_blockers;               ///< See setPointerBlockers.
        bool                   m_blockersKnown = false;  ///< Whether a UI has given them.

        std::vector<HeldPress> m_heldPresses;  ///< Every key and button down, with whose press it is, until
                                               ///< released. Few are held at once, so a scan beats a set.
};

} // namespace Vkm::Engine
