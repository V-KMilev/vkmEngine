#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "platform/input/input_command.h"

namespace Vkm::Engine {

class InputHandle;

/**
 * @brief What physically produces an action's value.
 *
 * The extension point: adding gamepad buttons and axes means a new value here
 * and a new case in InputMap::sample, with no change to any gameplay call site
 * - which is the whole reason bindings are data rather than a call to
 * isKeyPressed.
 */
enum class InputSource : uint8_t {
    Key,
    MouseButton,
};

/**
 * @brief One physical control contributing to an action.
 *
 * `scale` is what lets a single action be an axis: bind W at +1 and S at -1 and
 * "Move/Forward" reads -1..1 from two keys. For a plain button the scale is 1
 * and callers use held/pressed/released instead of axis().
 */
struct InputBinding {
    InputSource source = InputSource::Key;
    int         code   = 0;     ///< GLFW key or mouse-button code.
    float       scale  = 1.0f;  ///< Contribution to axis(); sign gives direction.
};

/**
 * @brief Named actions resolved from physical bindings, sampled once per frame.
 *
 * Gameplay asks for what it wants - "Jump", "Move/Forward" - instead of naming a
 * key. That indirection buys three things the direct calls cannot: bindings can
 * be changed at runtime (so a controls screen is possible), one action can carry
 * several bindings (keyboard and gamepad, or WASD and arrows) without the caller
 * knowing, and gameplay stops including the windowing library's headers.
 *
 * Sampling once a frame and keeping the previous value also makes
 * pressed()/released() correct for everyone, rather than each caller carrying
 * its own "was it down last frame" flag and getting it subtly wrong when two
 * call sites read the same key.
 *
 * Sampled by the engine at the top of the frame, before any system runs, so
 * every reader in the frame sees the same input state.
 *
 * That frame sampling serves frame-rate work - the camera, the editor, UI.
 * Simulation runs on a different clock, so a fixed update reads a per-tick
 * InputCommand instead, built by beginTick() from the edges latched across
 * however many frames fell between two ticks. The frame queries below must not
 * be read from a fixed update.
 */
class InputMap {
    public:
        InputMap() = default;
        ~InputMap() = default;

        InputMap(const InputMap& other) = delete;
        InputMap& operator=(const InputMap& other) = delete;

        InputMap(InputMap && other) = delete;
        InputMap& operator=(InputMap && other) = delete;

    public:
        /**
         * @brief Define (or replace) an action's bindings.
         *
         * @param action   Action name, e.g. "Move/Forward". Names are the stable
         *                 identity: they are what gameplay asks for and what a
         *                 saved binding file refers to.
         * @param bindings Physical controls feeding it.
         */
        void define(const std::string& action, std::vector<InputBinding> bindings);

        /**
         * @brief Add one binding to an action, defining it if new.
         *
         * The path a rebinding UI takes: bind a gamepad control alongside the
         * key rather than replacing it.
         */
        void addBinding(const std::string& action, InputBinding binding);

        /**
         * @brief Drop every binding for @p action, keeping the action defined.
         */
        void clearBindings(const std::string& action);

        /**
         * @brief Bindings currently feeding @p action; empty if undefined.
         *
         * For a controls screen to display and edit, and for saving.
         */
        const std::vector<InputBinding>& bindings(const std::string& action) const;

        /**
         * @brief Every defined action name, sorted.
         *
         * Sorted so a controls screen and a saved file both list them in a
         * stable order rather than hash order.
         */
        std::vector<std::string> actions() const;

        /**
         * @brief Sample every action from @p input, rolling the previous values.
         *
         * Called once per frame by the engine before the systems run. Also
         * latches this frame's edges for the next command, so a press that
         * begins and ends between two ticks still reaches one.
         */
        void update(const InputHandle& input);

        /**
         * @brief Build the command for @p tick from everything since the last one.
         *
         * Called once per fixed step by the engine, before the systems run.
         * Takes the axes as they stand and the edges latched since the previous
         * command, then clears the latch - which is what stops one keypress
         * reading as pressed on every tick of a slow frame.
         *
         * @param tick Simulation tick the command drives.
         */
        void beginTick(uint32_t tick);

        /**
         * @brief Say where the player is looking, for the commands that follow.
         *
         * Set from the frame clock, where a view actually turns, and read back
         * off InputCommand::view inside a fixed update. A game that steers
         * relative to its camera calls this once per frame after moving it; one
         * that steers in world space never calls it at all.
         *
         * @param view Orientation the input from now on is aimed with.
         */
        void setView(const glm::quat& view) { m_view = view; }

        /**
         * @brief The command the current fixed step is running under.
         *
         * What a fixed update reads instead of the frame queries. Before the
         * first beginTick() this is a zeroed command, which reads as "nothing
         * held", so a system that runs early sees no input rather than stale
         * input.
         */
        const InputCommand& command() const { return m_command; }

        /**
         * @brief The command's slot for @p action, or -1 when it has none.
         *
         * Assigned in definition order and stable for the session, which is
         * what lets a command be a fixed array rather than a map. An action
         * defined past MAX_INPUT_ACTIONS has no slot; its axis is absent from
         * every command and the frame queries still answer for it.
         *
         * @param action Action name.
         * @return Index into InputCommand::axis, or -1.
         */
        int indexOf(const std::string& action) const;

        /**
         * @brief Is the action active this frame? (any binding held)
         */
        bool held(const std::string& action) const;

        /**
         * @brief Did it become active this frame?
         */
        bool pressed(const std::string& action) const;

        /**
         * @brief Did it stop being active this frame?
         */
        bool released(const std::string& action) const;

        /**
         * @brief Summed, clamped value of the action's bindings, -1..1.
         *
         * Opposing keys cancel, which is what makes "both arrows held" resolve
         * to zero instead of favouring whichever was checked first.
         */
        float axis(const std::string& action) const;

    private:
        struct Action {
            std::vector<InputBinding> bindings;
            float value     = 0.0f;  ///< This frame's axis value.
            float lastValue = 0.0f;  ///< Previous frame's, for the edges.
            int   slot      = -1;    ///< Index in an InputCommand, or -1 past the cap.
        };

        /**
         * @brief Look up an action, or null when it was never defined.
         *
         * Querying an undefined action is not an error: a behavior may ask for
         * something the current binding set does not provide, and reading it as
         * inactive is more useful than a crash or a log flood.
         */
        const Action* find(const std::string& action) const;

        /**
         * @brief Give @p entry a command slot if it has none and one is left.
         *
         * @param entry Action being defined or extended.
         * @param action Its name, for the warning when the cap is reached.
         */
        void assignSlot(Action& entry, const std::string& action);

    private:
        std::unordered_map<std::string, Action> m_actions;

        InputCommand m_command;              ///< The current tick's, from beginTick.
        glm::quat    m_view{1.0f, 0.0f, 0.0f, 0.0f};  ///< Latched into every command beginTick builds.
        uint32_t     m_pendingPressed  = 0;  ///< Edges latched since the last command.
        uint32_t     m_pendingReleased = 0;
        uint32_t     m_nextSequence    = 1;  ///< 0 is the zeroed pre-first-tick command.
        uint32_t     m_nextSlot        = 0;
};

} // namespace Vkm::Engine
