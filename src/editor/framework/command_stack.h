#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "framework/command.h"

namespace Engine {

class Scene;
struct EditorState;

/**
 * @brief Bounded undo/redo history.
 *
 * push() takes a Command capturing a just-applied change and adds it to the
 * undo stack. The redo stack is cleared by any new push (industry-standard
 * "any edit invalidates redo history"). undo()/redo() move commands between
 * the two stacks while calling Command::undo / Command::redo on Scene+State.
 *
 * Owned by EditorState. Cleared on scene load - entity IDs and component
 * topology are no longer comparable across a scene swap.
 */
class CommandStack {
    public:
        CommandStack();
        ~CommandStack();

        CommandStack(const CommandStack&) = delete;
        CommandStack& operator=(const CommandStack&) = delete;
        CommandStack(CommandStack&&) = delete;
        CommandStack& operator=(CommandStack&&) = delete;

        /// Push a freshly applied command. Coalesces into the top of the
        /// undo stack if Command::tryMerge says so. Clears the redo stack.
        void push(std::unique_ptr<Command> cmd);

        /// Reverse the most recent command. No-op if the stack is empty.
        void undo(Scene& scene, EditorState& state);

        /// Re-apply the most recently undone command. No-op if redo is empty.
        void redo(Scene& scene, EditorState& state);

        /// Drop all history. Use on scene swap.
        void clear();

        bool canUndo() const { return !m_undo.empty(); }
        bool canRedo() const { return !m_redo.empty(); }

        /// Label of the command at the top of undo / redo (for the Edit menu).
        /// Returns nullptr if empty.
        const char* undoLabel() const;
        const char* redoLabel() const;

        size_t undoDepth() const { return m_undo.size(); }
        size_t redoDepth() const { return m_redo.size(); }

    private:
        std::vector<std::unique_ptr<Command>> m_undo;
        std::vector<std::unique_ptr<Command>> m_redo;

        /// Cap on the undo history to bound editor memory. Oldest entries
        /// are dropped when the cap is exceeded.
        static constexpr size_t kHistoryLimit = 200;
};

} // namespace Engine
