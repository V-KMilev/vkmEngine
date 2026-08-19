#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "framework/command.h"

namespace Vkm::Engine {

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
         * @brief Push a freshly applied command. Coalesces into the top of the
         * undo stack if Command::tryMerge says so. Clears the redo stack.
         */
        void push(std::unique_ptr<Command> cmd);

        /**
         * @brief Reverse the most recent command and move it to the redo stack.
         *
         * No-op if the undo stack is empty.
         *
         * @param scene Scene the command's reverse operation mutates.
         * @param state Editor state the command may touch (e.g. selection restore).
         */
        void undo(Scene& scene, EditorState& state);

        /**
         * @brief Re-apply the most recently undone command and move it back to undo.
         *
         * No-op if the redo stack is empty.
         *
         * @param scene Scene the command's redo operation mutates.
         * @param state Editor state the command may touch (e.g. selection restore).
         */
        void redo(Scene& scene, EditorState& state);

        /**
         * @brief Drop all undo and redo history.
         *
         * Used on scene swap, where entity IDs and component topology are no
         * longer comparable across the new scene.
         */
        void clear();

        bool canUndo() const { return !m_undo.empty(); }
        bool canRedo() const { return !m_redo.empty(); }

        /**
         * @brief Label of the command at the top of undo / redo (for the Edit menu).
         * Returns nullptr if empty.
         */
        const char* undoLabel() const;
        const char* redoLabel() const;

        size_t undoDepth() const { return m_undo.size(); }
        size_t redoDepth() const { return m_redo.size(); }

    private:
        std::vector<std::unique_ptr<Command>> m_undo;
        std::vector<std::unique_ptr<Command>> m_redo;

        /**
         * @brief Cap on the undo history to bound editor memory. Oldest entries
         * are dropped when the cap is exceeded.
         */
        static constexpr size_t HISTORY_LIMIT = 200;
};

} // namespace Vkm::Engine
