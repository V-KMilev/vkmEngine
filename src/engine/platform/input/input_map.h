#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {

class InputHandle;

/**
 * @brief What physically produces an action's value.
 *
 * Only keyboard and mouse exist today. The enum is the extension point: adding
 * gamepad buttons and axes means a new value here and a new case in
 * InputMap::sample, with no change to any gameplay call site - which is the
 * whole reason bindings are data rather than a call to isKeyPressed.
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
 * It also removes the edge-detection bookkeeping every input site otherwise
 * writes for itself. Sampling once a frame and keeping the previous value makes
 * pressed()/released() correct for everyone, rather than each caller carrying
 * its own "was it down last frame" flag and getting it subtly wrong when two
 * call sites read the same key.
 *
 * Sampled by the engine at the top of the frame, before any system runs, so
 * every reader in the frame sees the same input state.
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
         * Called once per frame by the engine before the systems run.
         */
        void update(const InputHandle& input);

        /** @brief Is the action active this frame? (any binding held) */
        bool held(const std::string& action) const;

        /** @brief Did it become active this frame? */
        bool pressed(const std::string& action) const;

        /** @brief Did it stop being active this frame? */
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
        };

        /**
         * @brief Look up an action, or null when it was never defined.
         *
         * Querying an undefined action is not an error: a behavior may ask for
         * something the current binding set does not provide, and reading it as
         * inactive is more useful than a crash or a log flood.
         */
        const Action* find(const std::string& action) const;

    private:
        std::unordered_map<std::string, Action> m_actions;
};

} // namespace Engine
