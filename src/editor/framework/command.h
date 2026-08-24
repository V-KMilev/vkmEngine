#pragma once

#include <cstdint>

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
         * @return A string that outlives the command - every implementation
         *         stores the pointer it was constructed with rather than a
         *         copy, and CommandStack::undoLabel() hands it to the menu bar
         *         frames later. A literal. Not a std::string's c_str().
         */
        virtual const char* label() const = 0;

        /**
         * @brief Optionally absorb @p incoming into @c this and return true.
         *
         * Called when @p incoming is being pushed, the top of the undo stack
         * is @c this, and both belong to the same gesture. Used to coalesce
         * the micro-edits *within* one gesture - a gizmo drag pushing per
         * frame, a stream of inspector drag-float steps - into one undoable
         * step. Implementations therefore test identity only: the gesture
         * boundary is CommandStack's to enforce (see CommandStack::endGesture),
         * because a command cannot see the mouse.
         *
         * @param incoming The command being pushed.
         * @return true if @p incoming was merged and should be discarded by
         *         the stack; false to keep both as separate entries.
         */
        virtual bool tryMerge(Command& incoming) { return false; }

        /**
         * @brief Whether undoing or redoing this step lands on the entity in
         * slot @p slotIndex.
         *
         * Answered by the steps that address an entity, so the history can drop
         * the ones an operation has outlived rather than dropping all of them.
         * Save as Prefab is the caller: the subtree it writes becomes the
         * prefab's, and a step that assigns a component in there would undo to
         * a value the scene no longer stores. Steps that address no entity -
         * an asset rename, a scene-global setting - answer false and survive.
         *
         * Keyed by slot rather than by EntityId for the reason liveEntity() is:
         * the slot is the half of the id that a destroy-and-resurrect keeps.
         *
         * @param slotIndex Entity slot the caller is asking about.
         * @return true when this step's effect lands on that entity.
         */
        virtual bool addresses(uint32_t slotIndex) const { return false; }
};

} // namespace Vkm::Engine
