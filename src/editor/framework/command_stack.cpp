#include "framework/command_stack.h"

namespace Engine {

CommandStack::CommandStack()  = default;
CommandStack::~CommandStack() = default;

void CommandStack::push(std::unique_ptr<Command> cmd) {
    if (!cmd) return;

    // A fresh edit invalidates the redo history. Drop it before merge so a
    // failed merge doesn't leave a half-state.
    m_redo.clear();

    if (!m_undo.empty() && m_undo.back()->tryMerge(*cmd)) {
        // The top of the stack absorbed the incoming command. Drop the
        // incoming (it goes out of scope here).
        return;
    }

    m_undo.push_back(std::move(cmd));

    // Bound the history. Drop oldest entries when the cap is exceeded.
    if (m_undo.size() > kHistoryLimit) {
        m_undo.erase(m_undo.begin(),
            m_undo.begin() + (m_undo.size() - kHistoryLimit));
    }
}

void CommandStack::undo(Scene& scene, EditorState& state) {
    if (m_undo.empty()) return;
    auto cmd = std::move(m_undo.back());
    m_undo.pop_back();
    cmd->undo(scene, state);
    m_redo.push_back(std::move(cmd));
}

void CommandStack::redo(Scene& scene, EditorState& state) {
    if (m_redo.empty()) return;
    auto cmd = std::move(m_redo.back());
    m_redo.pop_back();
    cmd->redo(scene, state);
    m_undo.push_back(std::move(cmd));
}

void CommandStack::clear() {
    m_undo.clear();
    m_redo.clear();
}

const char* CommandStack::undoLabel() const {
    return m_undo.empty() ? nullptr : m_undo.back()->label();
}

const char* CommandStack::redoLabel() const {
    return m_redo.empty() ? nullptr : m_redo.back()->label();
}

} // namespace Engine
