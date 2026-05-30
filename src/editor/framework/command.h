#pragma once

namespace Engine {

class Scene;
struct EditorState;

/**
 * @brief Reversible editor mutation.
 *
 * Each user action that mutates the scene (gizmo drag, add/remove component,
 * inspector field edit, entity create/destroy) creates and pushes a Command
 * onto EditorState's CommandStack. The caller has already applied the change
 * before pushing - push() just captures the reverse operation. redo() re-runs
 * the change after an undo.
 *
 * Commands are passed both Scene (the data they mutate) and EditorState (for
 * cross-cutting bits like selection restoration on undo of a destroy).
 *
 * tryMerge lets a stream of micro-changes (e.g. an inspector drag-float over
 * many frames) collapse into a single undoable step.
 */
class Command {
    public:
        virtual ~Command() = default;

        /// Re-apply the change. Called when the user presses redo.
        virtual void redo(Scene& scene, EditorState& state) = 0;

        /// Reverse the change. Called when the user presses undo.
        virtual void undo(Scene& scene, EditorState& state) = 0;

        /// Short human-readable name for the action (used in Edit menu).
        virtual const char* label() const = 0;

        /**
         * @brief Optionally absorb @p incoming into @c this and return true.
         *
         * Called when @p incoming is being pushed and the top of the undo
         * stack is @c this. Used to coalesce a stream of related micro-edits
         * (e.g. consecutive transform tweaks on the same entity within a
         * short time window) into one undoable step.
         *
         * @return true if @p incoming was merged and should be discarded by
         *         the stack; false to keep both as separate entries.
         */
        virtual bool tryMerge(Command& incoming) { return false; }
};

} // namespace Engine
