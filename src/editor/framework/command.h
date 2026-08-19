#pragma once

namespace Vkm::Engine {

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
 */
class Command {
    public:
        virtual ~Command() = default;

        /**
         * @brief Re-apply the change after an undo.
         *
         * @param scene Scene the change is replayed against.
         * @param state Editor state updated alongside the change (e.g. selection).
         */
        virtual void redo(Scene& scene, EditorState& state) = 0;

        /**
         * @brief Reverse the previously applied change.
         *
         * @param scene Scene the reverse operation mutates.
         * @param state Editor state restored alongside the reversal (e.g. selection).
         */
        virtual void undo(Scene& scene, EditorState& state) = 0;

        /**
         * @brief Short human-readable name for the action.
         *
         * Surfaced in the Edit menu's Undo/Redo entries.
         *
         * @return Stable label string owned by the command.
         */
        virtual const char* label() const = 0;

        /**
         * @brief Optionally absorb @p incoming into @c this and return true.
         *
         * Called when @p incoming is being pushed and the top of the undo
         * stack is @c this. Used to coalesce a stream of related micro-edits
         * (e.g. consecutive transform tweaks on the same entity) into one
         * undoable step. Implementations merge on identity only - there is no
         * time gate, so two separate edits of the same target with nothing
         * pushed between them collapse into a single step.
         *
         * @return true if @p incoming was merged and should be discarded by
         *         the stack; false to keep both as separate entries.
         */
        virtual bool tryMerge(Command& incoming) { return false; }
};

} // namespace Vkm::Engine
