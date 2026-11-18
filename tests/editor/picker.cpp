// The file picker the imports open: a click picks one file, Ctrl and Shift pick several, and Open hands
// them over in list order.

#include <filesystem>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "ui/file_picker.h"

#include "editor/desk.h"
#include "harness/check.h"

using namespace Vkm::Test;

namespace {

/// The picker's popup, or with @p list the list inside it, whose name only begins with the popup's.
ImGuiWindow* pickerWindow(bool list) {
    for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows) {
        const std::string name = window->Name;
        if (list ? name.find("##AssetPickerList") != std::string::npos : name == "Import Texture") return window;
    }
    return nullptr;
}

/**
 * @brief A picker of four images, opened and drawn once, as the texture import opens it.
 */
struct Picking {
    Pointer                  pointer;
    FilePicker               picker;
    std::vector<std::string> picks;
    bool                     picked = false;

    explicit Picking(bool many) {
        const std::filesystem::path root = scratch() / "images";
        for (const char* name : {"a.png", "b.png", "c.png", "d.png"}) writeText(root / name, "");
        FilePicker::Options options;
        options.title      = "Import Texture";
        options.root       = root;
        options.extensions = {".png"};
        options.relativeTo = root;
        options.many       = many;
        picker.open(options);
        // Two frames: the search field takes the keyboard the frame after the popup appears.
        draw();
        draw();
    }

    void draw() {
        pointer.frame([&] { picked = picker.draw(picks) || picked; });
    }

    /// Click @p row's line as with @p held down (ImGuiMod_Ctrl, ImGuiMod_Shift or none), over two frames.
    void click(const char* row, ImGuiKey held = ImGuiKey_None) {
        if (held != ImGuiKey_None) pointer.key(held, true);
        ImGui::ActivateItemByID(pickerWindow(true)->GetID(row));
        draw();
        if (held != ImGuiKey_None) pointer.key(held, false);
        draw();
    }

    void open() {
        ImGui::ActivateItemByID(pickerWindow(false)->GetID("Open"));
        draw();
    }
};

} // namespace

TEST("the import picker adds a file to the pick with Ctrl, and hands them over in list order") {
    Picking picking(true);
    picking.click("c.png");
    picking.click("a.png", ImGuiMod_Ctrl);
    picking.open();
    REQUIRE(picking.picked);
    CHECK(picking.picks == std::vector<std::string>{"a.png", "c.png"});
}

TEST("the import picker picks a run with Shift, and Ctrl takes one back out") {
    Picking range(true);
    range.click("b.png");
    range.click("d.png", ImGuiMod_Shift);
    range.click("c.png", ImGuiMod_Ctrl);
    range.open();
    REQUIRE(range.picked);
    CHECK(range.picks == std::vector<std::string>{"b.png", "d.png"});
}

TEST("a picker for one file keeps one, whatever key is held") {
    Picking picking(false);
    picking.click("c.png");
    picking.click("a.png", ImGuiMod_Ctrl);
    picking.open();
    REQUIRE(picking.picked);
    CHECK(picking.picks == std::vector<std::string>{"a.png"});
}
