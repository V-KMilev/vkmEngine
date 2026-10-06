#pragma once

#include <algorithm>
#include <cfloat>

#include <imgui.h>

#include "ui/editor_style.h"

namespace Vkm::Engine {

enum class DialogResult {
    None,     ///< Nothing chosen; the dialog stays open.
    Confirm,  ///< Confirm button or Enter.
    Alt,      ///< Optional third action ("Don't Save").
    Cancel,   ///< Cancel button or Escape.
};

/**
 * @brief Open (when @p wantOpen) and begin the centered modal, first of the dialog scaffold's three calls.
 *
 *   if (beginDialog("Rename Asset", m_renameOpen)) {
 *       // ...content...
 *       switch (dialogButtons(m_renameOpen, "Rename", nameValid)) { ... }
 *       endDialog();
 *   }
 *
 * @param title    ImGui title and popup id.
 * @param wantOpen Dialog-visible intent; cleared when dismissed by a path that skipped dialogButtons.
 * @param initialSize First-open size in screen pixels (px() of a design size); zero fits the content.
 * @param minimumSize Smallest drag size; read only with @p initialSize.
 * @return Whether the modal is open; content, dialogButtons and endDialog run only when true.
 */
inline bool beginDialog(
    const char* title,
    bool& wantOpen,
    ImVec2 initialSize = ImVec2(0.0f, 0.0f),
    ImVec2 minimumSize = ImVec2(0.0f, 0.0f)
) {
    if (wantOpen && !ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings;
    if (initialSize.x > 0.0f) {
        ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(minimumSize, ImVec2(FLT_MAX, FLT_MAX));
    } else {
        flags |= ImGuiWindowFlags_AlwaysAutoResize;
    }

    const bool open = ImGui::BeginPopupModal(title, nullptr, flags);
    if (!open) wantOpen = false;
    return open;
}

/**
 * @brief Begin a floating tool window: centred and sized on first use, never docked.
 *
 * A closed or collapsed window is ended here, so the caller pairs ImGui::End only
 * with a true return.
 *
 * @param name Window title and id.
 * @param show Visibility flag; the window's close button clears it.
 * @param size First-use size in screen pixels (px() of a design size).
 * @return Whether the window is open.
 */
inline bool beginToolWindow(const char* name, bool& show, ImVec2 size) {
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
    if (ImGui::Begin(name, &show, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking)) return true;
    ImGui::End();
    return false;
}

/**
 * @brief The three-way dialog button row: [alt] [Cancel] [Confirm], right-aligned.
 *
 * Any result closes the popup and clears @p wantOpen. Escape cancels; Enter confirms
 * when no text field has the keyboard or @p fieldCommitted, since an active field's
 * WantTextInput would swallow Enter. The alt label has no default and precedes the
 * flags, so only a label can land in its slot.
 *
 * @param wantOpen       The flag beginDialog received.
 * @param confirmLabel   Rightmost (accent, default) action.
 * @param altLabel       Third action left of Cancel, or nullptr.
 * @param confirmEnabled Gates the button and both Enter paths.
 * @param fieldCommitted A text field here returned true from EnterReturnsTrue this frame.
 * @return What fired this frame (None while the dialog stays open).
 */
inline DialogResult dialogButtons(
    bool& wantOpen,
    const char* confirmLabel,
    const char* altLabel,
    bool confirmEnabled = true,
    bool fieldCommitted = false
) {
    const ImGuiStyle& style = ImGui::GetStyle();

    // One width for the whole row: the widest label, floored at 96 design px.
    float bw = EditorStyle::px(96.0f);
    auto fit = [&](const char* label) {
        if (label) bw = std::max(bw, ImGui::CalcTextSize(label).x + style.FramePadding.x * 4.0f);
    };
    fit(confirmLabel);
    fit("Cancel");
    fit(altLabel);

    const int   n     = altLabel ? 3 : 2;
    const float total = n * bw + (n - 1) * style.ItemSpacing.x;

    ImGui::Spacing();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - total));

    DialogResult r = DialogResult::None;
    if (altLabel) {
        if (ImGui::Button(altLabel, ImVec2(bw, 0))) r = DialogResult::Alt;
        ImGui::SameLine();
    }
    if (ImGui::Button("Cancel", ImVec2(bw, 0))) r = DialogResult::Cancel;
    ImGui::SameLine();
    ImGui::BeginDisabled(!confirmEnabled);
    ImGui::PushStyleColor(ImGuiCol_Button,        EditorStyle::ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorStyle::ACCENT_HOV);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  EditorStyle::ACCENT);
    if (ImGui::Button(confirmLabel, ImVec2(bw, 0))) r = DialogResult::Confirm;
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();

    if (r == DialogResult::None) {
        const bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter)
            || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
        const bool enterConfirms = fieldCommitted || (!ImGui::GetIO().WantTextInput && enterPressed);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            r = DialogResult::Cancel;
        } else if (confirmEnabled && enterConfirms) {
            r = DialogResult::Confirm;
        }
    }

    if (r != DialogResult::None) {
        wantOpen = false;
        ImGui::CloseCurrentPopup();
    }
    return r;
}

/**
 * @brief The two-button dialog button row: [Cancel] [Confirm], right-aligned.
 *
 * @param wantOpen       The flag beginDialog received.
 * @param confirmLabel   Rightmost (accent, default) action.
 * @param confirmEnabled Gates the button and both Enter paths.
 * @param fieldCommitted A text field here returned true from EnterReturnsTrue this frame.
 * @return What fired this frame (None while the dialog stays open).
 */
inline DialogResult dialogButtons(
    bool& wantOpen,
    const char* confirmLabel,
    bool confirmEnabled = true,
    bool fieldCommitted = false
) {
    return dialogButtons(wantOpen, confirmLabel, nullptr, confirmEnabled, fieldCommitted);
}

/**
 * @brief Refuse a call that puts a flag ahead of the labels.
 *
 * dialogButtons(want, "Save", true, "Don't Save") would otherwise bind the label to
 * fieldCommitted on the two-button overload; this exact match outranks that conversion.
 *
 * @param wantOpen       The flag beginDialog received.
 * @param confirmLabel   Rightmost action.
 * @param confirmEnabled Gates the button.
 * @param altLabel       Third action, which belongs before the flags.
 * @return Nothing: the call does not compile.
 */
inline DialogResult dialogButtons(
    bool& wantOpen,
    const char* confirmLabel,
    bool confirmEnabled,
    const char* altLabel
) = delete;

/**
 * @brief End the modal begun by a true-returning beginDialog.
 */
inline void endDialog() { ImGui::EndPopup(); }

/**
 * @brief The editor's one rename dialog: a name field and the two buttons.
 *
 * The field takes focus with the old name selected. The caller owns the buffer and does
 * the renaming; any close clears @p open.
 *
 * @param title Modal title and popup id.
 * @param open Dialog-visible intent; the caller sets it to raise the dialog.
 * @param buf Edit buffer, seeded with the current name.
 * @param bufSize Size of @p buf.
 * @return True on the frame Rename is confirmed with a non-empty name.
 */
inline bool renameDialog(const char* title, bool& open, char* buf, size_t bufSize) {
    if (!beginDialog(title, open)) return false;

    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(EditorStyle::px(280.0f));
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue
        | ImGuiInputTextFlags_AutoSelectAll;
    const bool committed = ImGui::InputText("##rnbuf", buf, bufSize, flags);
    const DialogResult result = dialogButtons(open, "Rename", buf[0] != '\0', committed);
    endDialog();
    return result == DialogResult::Confirm;
}

} // namespace Vkm::Engine
