#pragma once

#include <cstddef>
#include <cstdint>
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
         * undo stack if the gesture is still open and Command::tryMerge says
         * so. Clears the redo stack.
         */
        void push(std::unique_ptr<Command> cmd);

        /**
         * @brief Seal the top of the undo stack: nothing pushed later merges
         * into it.
         *
         * A gesture is a press, a motion and a release, and it is one undo
         * step. Coalescing exists to collapse the micro-edits inside one - a
         * gizmo drag that pushes on release, an inspector drag-float that
         * pushes every changed frame - and identity alone cannot tell those
         * apart from two separate drags of the same field minutes apart. The
         * editor calls this once the pointer is up and no widget is active, so
         * the boundary is drawn where the user drew it.
         */
        void endGesture() { m_mergeOpen = false; }

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

        /**
         * @brief Drop the entries that address any entity in @p slotIndices.
         *
         * The narrow half of clear(), for an operation that outlives part of
         * the history rather than all of it. Save as Prefab is the caller:
         * the subtree it writes stops being the scene's to describe, while
         * every edit to every other entity still is.
         *
         * @param slotIndices Entity slots whose steps are no longer meaningful.
         */
        void forget(const std::vector<uint32_t>& slotIndices);

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

        /**
         * @brief How many times the history has changed, ever.
         *
         * Bumped by every push, undo, redo and drop. A caller that recorded
         * this number and reads the same one back knows the history is exactly
         * the sequence it left - which the depths cannot say, since an undo and
         * a redo return to the same depth, and a push at the history limit
         * evicts an entry without changing one.
         *
         * @return Monotonic revision stamp for the whole history.
         */
        unsigned long long revision() const { return m_revision; }

    private:
        /**
         * @brief Cap on the undo history to bound editor memory. Oldest entries
         * are dropped when the cap is exceeded.
         */
        static constexpr size_t HISTORY_LIMIT = 200;

    private:
        std::vector<std::unique_ptr<Command>> m_undo;
        std::vector<std::unique_ptr<Command>> m_redo;

        /**
         * @brief Whether the top of the undo stack may still absorb a push.
         *
         * True from a push until the editor reports the gesture over, so the
         * window a merge can happen in is exactly one gesture wide.
         */
        bool m_mergeOpen = false;

        /**
         * @brief Bumped by every operation that changes the history.
         */
        unsigned long long m_revision = 0;
};

} // namespace Vkm::Engine
