#pragma once

#include <memory>
#include <string>

#include "command/command_stack.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief How loudly a message to the author is said, and for how long.
 */
enum class ToastKind { Info, Warning, Error };

/**
 * @brief What an undoable step may touch outside the scene it edits.
 *
 * Not the whole EditorState, which carries ImGui types, so undo runs without a
 * window; tests/editor/undo_tests.cpp supplies its own.
 */
class CommandHost {
    public:
        CommandHost() = default;
        virtual ~CommandHost() = default;

        CommandHost(const CommandHost& other) = delete;
        CommandHost& operator=(const CommandHost& other) = delete;

        CommandHost(CommandHost && other) = delete;
        CommandHost& operator=(CommandHost && other) = delete;

    public:
        /// The scene no longer matches what is on disk.
        virtual void markSceneDirty() = 0;

        /**
         * @brief Say something to the author.
         *
         * @param kind How loudly.
         * @param message Text shown.
         * @param seconds Duration; 0 takes it from @p kind.
         */
        virtual void pushToast(ToastKind kind, std::string message, float seconds = 0.0f) = 0;

        /**
         * @brief Make @p id the one selected entity.
         *
         * @param id Replaces the whole selection.
         */
        virtual void selectEntity(EntityId id) = 0;

        /// The stack, for a step that produces another step.
        virtual CommandStack& commandStack() = 0;

        /**
         * @brief Record a step whose change has already been applied, and mark
         *        the scene unsaved.
         *
         * The stack alone does not raise the dirty flag. Only an edit that marks
         * the scene as it goes pushes onto commandStack() directly.
         *
         * @param step Applied change, held to undo it.
         */
        void pushStep(std::unique_ptr<Command> step) {
            commandStack().push(std::move(step));
            markSceneDirty();
        }
};

} // namespace Vkm::Engine
