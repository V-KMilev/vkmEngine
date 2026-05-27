#define VKM_LOG_CATEGORY "EDITOR"

#include "framework/command_stack.h"

#include "logger.h"

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
        LOG_VERBOSE("Merged into '%s' (undo size %zu)",
            m_undo.back()->label(), m_undo.size());
        return;
    }

    const char* label = cmd->label();
    m_undo.push_back(std::move(cmd));

    // Bound the history. Drop oldest entries when the cap is exceeded.
    if (m_undo.size() > kHistoryLimit) {
        const size_t dropped = m_undo.size() - kHistoryLimit;
        m_undo.erase(m_undo.begin(), m_undo.begin() + dropped);
        LOG_WARNING("History limit %zu reached; dropped %zu oldest command(s)",
            kHistoryLimit, dropped);
    }
    LOG_VERBOSE("Pushed '%s' (undo size %zu)", label, m_undo.size());
}

void CommandStack::undo(Scene& scene, EditorState& state) {
    if (m_undo.empty()) return;
    auto cmd = std::move(m_undo.back());
    m_undo.pop_back();
    LOG_VERBOSE("Undo '%s' (undo %zu -> redo %zu)",
        cmd->label(), m_undo.size(), m_redo.size() + 1);
    cmd->undo(scene, state);
    m_redo.push_back(std::move(cmd));
}

void CommandStack::redo(Scene& scene, EditorState& state) {
    if (m_redo.empty()) return;
    auto cmd = std::move(m_redo.back());
    m_redo.pop_back();
    LOG_VERBOSE("Redo '%s' (redo %zu -> undo %zu)",
        cmd->label(), m_redo.size(), m_undo.size() + 1);
    cmd->redo(scene, state);
    m_undo.push_back(std::move(cmd));
}

void CommandStack::clear() {
    if (!m_undo.empty() || !m_redo.empty()) {
        LOG_VERBOSE("Cleared (dropped %zu undo + %zu redo)",
            m_undo.size(), m_redo.size());
    }
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
