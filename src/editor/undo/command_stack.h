#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "command/command.h"

namespace Vkm::Engine {

class Scene;
class CommandHost;

/**
 * @brief Bounded undo/redo history.
 *
 * Owned by EditorState.
 */
class CommandStack {
    public:
        CommandStack();
        ~CommandStack();

        CommandStack(const CommandStack& other) = delete;
        CommandStack& operator=(const CommandStack& other) = delete;

        CommandStack(CommandStack && other) = delete;
        CommandStack& operator=(CommandStack && other) = delete;

        /**
         * @brief Push a freshly applied command.
         *
         * Coalesces into the top of the undo stack if the gesture is still open
         * and Command::tryMerge says so. Clears the redo stack.
         *
         * @param cmd The applied change; null is ignored.
         */
        void push(std::unique_ptr<Command> cmd);

        /**
         * @brief Seal the top of the undo stack: nothing pushed later merges
         * into it.
         *
         * A gesture is a press, a motion and a release, and it is one undo
         * step. Coalescing collapses the micro-edits inside one, and identity
         * alone cannot tell those apart from two separate drags of the same
         * field minutes apart. Call it once the pointer is up and no widget is
         * active.
         */
        void endGesture() { m_mergeOpen = false; }

        /**
         * @brief Reverse the most recent command and move it to the redo stack.
         *
         * No-op if the undo stack is empty.
         *
         * @param scene Scene the command's reverse operation mutates.
         * @param host  What the command may touch outside the scene (e.g. selection).
         */
        void undo(Scene& scene, CommandHost& host);

        /**
         * @brief Re-apply the most recently undone command and move it back to undo.
         *
         * No-op if the redo stack is empty.
         *
         * @param scene Scene the command's redo operation mutates.
         * @param host  What the command may touch outside the scene (e.g. selection).
         */
        void redo(Scene& scene, CommandHost& host);

        /**
         * @brief Drop all undo and redo history, a parked one included.
         *
         * For a scene swap, where entity IDs and component topology are no
         * longer comparable across the new scene.
         */
        void clear();

        /**
         * @brief Set the history aside and take the steps that follow on an
         *        empty one.
         *
         * A play session edits the simulation's copy of the world, which Stop
         * throws away. Its steps go on a history of their own, so an undo
         * inside the session cannot reach past them into authored work, and
         * the authored history outlives whatever the session pushed. Ignored
         * while a history is already parked.
         */
        void park();

        /**
         * @brief Discard every step taken since park() and put the parked
         *        history back.
         *
         * No-op when nothing is parked.
         */
        void unpark();

        /// Whether park() has set a history aside that unpark() has not restored.
        bool isParked() const { return m_parked; }

        /**
         * @brief Drop the entries that address any entity in @p slotIndices.
         *
         * The narrow half of clear(), for an operation that outlives part of
         * the history rather than all of it.
         *
         * @param slotIndices Entity slots whose steps are no longer meaningful.
         */
        void forget(const std::vector<uint32_t>& slotIndices);

        bool canUndo() const { return !m_undo.empty(); }
        bool canRedo() const { return !m_redo.empty(); }

        /**
         * @brief Label of the command at the top of undo.
         *
         * @return The label, or nullptr when there is nothing to undo.
         */
        const char* undoLabel() const;

        /**
         * @brief Label of the command at the top of redo.
         *
         * @return The label, or nullptr when there is nothing to redo.
         */
        const char* redoLabel() const;

        size_t undoDepth() const { return m_undo.size(); }
        size_t redoDepth() const { return m_redo.size(); }

    private:
        /**
         * @brief Cap on the undo history to bound editor memory.
         */
        static constexpr size_t HISTORY_LIMIT = 200;

    private:
        std::vector<std::unique_ptr<Command>> m_undo;
        std::vector<std::unique_ptr<Command>> m_redo;

        /**
         * @brief Whether the top of the undo stack may still absorb a push.
         *
         * True from a push until the editor reports the gesture over, or the
         * stack changes under it in any other way.
         */
        bool m_mergeOpen = false;

        std::vector<std::unique_ptr<Command>> m_parkedUndo;  ///< The authored history while a session runs.
        std::vector<std::unique_ptr<Command>> m_parkedRedo;
        bool m_parked = false;
};

} // namespace Vkm::Engine
