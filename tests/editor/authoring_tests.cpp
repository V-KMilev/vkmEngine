#include "support.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>

#include "ecs/component/core/net_local.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/hierarchy_operations.h"
#include "io/scene/prefab.h"
#include "resource/resource_manager.h"
#include "system/render/probe_fill.h"

#include "session/file_import.h"
#include "copy_parent.h"
#include "editor_state.h"
#include "mesh_pick.h"
#include "overlays/click_target.h"
#include "overlays/gizmo_snap.h"
#include "overlays/ui_rect_drag.h"
#include "session/content_pack.h"
#include "session/log_tail.h"
#include "ui/field_label.h"

namespace {

// What the bake and the editor compute and then trust unchecked: a badly repaired probe grid
// is a glowing wall, a bad snap a drifting transform, a bad pick the room instead
// of the crate in it.

// A UI element's rect drags as other editors': its body moves it, an edge resizes it with
// the opposite side held where it stood, whatever the pivot.
void testAUIRectResizesFromItsHeldSide() {
    std::printf("Dragging a UI element's rect:\n");

    UIElement start;  // pivot at the centre, 100 x 100
    const auto leftEdge  = [](const UIElement& e) { return e.position.x - e.pivot.x * e.size.x; };
    const auto rightEdge = [&](const UIElement& e) { return leftEdge(e) + e.size.x; };

    const UIElement wider = dragRect(start, RectHandle::Right, {20.0f, 0.0f});
    check(
        "its right edge pulled, the left stays",
        wider.size.x == 120.0f && leftEdge(wider) == leftEdge(start)
    );
    const UIElement narrower = dragRect(start, RectHandle::Left, {30.0f, 0.0f});
    check(
        "  its left pushed in, the right stays",
        narrower.size.x == 70.0f && rightEdge(narrower) == rightEdge(start)
    );
    const UIElement corner = dragRect(start, RectHandle::BottomRight, {10.0f, 20.0f});
    check("a corner resizes both ways", corner.size == glm::vec2(110.0f, 120.0f));
    const UIElement moved = dragRect(start, RectHandle::Body, {3.4f, -7.6f});
    check(
        "its body moves it, to whole pixels",
        moved.position == glm::vec2(3.0f, -8.0f) && moved.size == start.size
    );
    check("never narrower than a pixel", dragRect(start, RectHandle::Left, {500.0f, 0.0f}).size.x == 1.0f);

    // Stretched across its parent, size is an inset: it goes on below a pixel, never jumps to one.
    UIElement stretched;
    stretched.relativeSize = {1.0f, 0.0f};
    stretched.size.x       = -40.0f;
    check(
        "  but a stretched one's inset goes on growing",
        dragRect(stretched, RectHandle::Left, {10.0f, 0.0f}).size.x == -50.0f
    );
}

// The Console follows the log file as the logger writes it: each line split where vkmLog
// joins it, a half-written line held for its end, and nothing from before the run.
void testTheConsoleFollowsTheLog() {
    std::printf("What the Console reads of the log:\n");
    namespace fs = std::filesystem;

    const LogLine tagged =
        parseLogLine("[2026-10-20 05:17:33.783] [VKM-ENGINE] [WINDOW] [WARNING] FPS cap = 15");
    check(
        "a line is split into its time, category, level and message",
        tagged.time == "2026-10-20 05:17:33.783" && tagged.category == "WINDOW"
            && tagged.severity == LogSeverity::Warning && tagged.text == "FPS cap = 15"
    );
    const LogLine bare = parseLogLine("[2026-10-20 05:17:33.783] [VKM-ENGINE] [ERROR] [x] in the text");
    check(
        "  one without a category, its message's own brackets kept",
        bare.category.empty() && bare.severity == LogSeverity::Error && bare.text == "[x] in the text"
    );
    check("a line of another shape is kept whole", parseLogLine("plain words").text == "plain words");
    const LogLine quoting = parseLogLine("[t] [S] [INFO] [ERROR] was the word");
    check(
        "a message quoting a level keeps the line's own",
        quoting.severity == LogSeverity::Info && quoting.text == "[ERROR] was the word"
    );

    const fs::path file = runScratch() / "vkm_console_test.log";
    std::error_code ec;
    fs::remove(file, ec);
    std::ofstream(file) << "[t] [S] [INFO] before the run\n";
    LogTail tail;
    tail.open(file, fs::file_size(file, ec));
    std::ofstream(file, std::ios::app) << "[t] [S] [INFO] first\n[t] [S] [DEBUG] half";
    tail.poll();
    check(
        "only what the run wrote arrives, a half line held back",
        tail.lines().size() == 1 && tail.lines().front().text == "first"
    );
    std::ofstream(file, std::ios::app) << " done\n";
    tail.poll();
    check(
        "  and joined when its end is written",
        tail.lines().size() == 2 && tail.lines().back().text == "half done"
    );

    const size_t debugs = tail.counts()[static_cast<size_t>(LogSeverity::Debug)];
    check("  each severity counted as it comes", debugs == 1);
    const uint64_t seen = tail.revision();
    tail.clear();
    check(
        "  and forgotten with the lines, the change told",
        tail.counts()[static_cast<size_t>(LogSeverity::Info)] == 0 && tail.revision() != seen
    );

    std::ofstream(file, std::ios::trunc) << "[t] [S] [INFO] again\n";
    tail.poll();
    check("a file started over is read from its start", tail.lines().back().text == "again");

    const uint64_t followed = tail.revision();
    tail.open(file, fs::file_size(file, ec));
    const size_t infos = tail.counts()[static_cast<size_t>(LogSeverity::Info)];
    check(
        "opened again, it keeps nothing of what it followed, the change told",
        tail.lines().empty() && infos == 0 && tail.dropped() == 0 && tail.revision() != followed
    );
    fs::remove(file, ec);

    // A line vkmLog itself writes, into the file the run logs to (tests/main.cpp), read back whole:
    // its format is vkmLog's, which the split restates.
    const fs::path ran = fs::temp_directory_path() / "vkm_engine_tests.log";
    LogTail logged;
    logged.open(ran, fs::file_size(ran, ec));
    Vkm::Log::Logger::getInstance().log(Vkm::Log::LogLevel::FATAL, nullptr, "ROUND", "a [held] line %d", 7);
    logged.poll();
    const bool read = !logged.lines().empty() && logged.lines().back().severity == LogSeverity::Fatal
        && logged.lines().back().category == "ROUND" && logged.lines().back().text == "a [held] line 7"
        && logged.lines().back().time.size() == std::string("YYYY-MM-DD HH:MM:SS.mmm").size();
    check("a line vkmLog writes comes back as it was logged", read);
}

// A click on a placed thing selects the thing, not the part under the pointer; the next
// click, made inside it, goes one level in.
void testAClickSelectsThePlacedThingBeforeItsParts() {
    std::printf("What a viewport click selects inside a prefab instance:\n");

    Scene scene;
    const EntityId house = scene.createEntity();
    const EntityId door  = scene.createEntity();
    const EntityId knob  = scene.createEntity();
    const EntityId loose = scene.createEntity();
    scene.add(house, PrefabInstance{});
    scene.add(door, PrefabInstance{});  // a prefab nested in the house
    HierarchyOperations::setParent(scene, door, house);
    HierarchyOperations::setParent(scene, knob, door);

    check("a part selects the outermost instance first", clickTarget(scene, knob, {}) == house);
    check("  then the instance nested in it", clickTarget(scene, knob, house) == door);
    check("  then the part itself", clickTarget(scene, knob, door) == knob);
    check("  and stays on it", clickTarget(scene, knob, knob) == knob);
    check("from another thing, it starts at the outside again", clickTarget(scene, knob, loose) == house);
    check("an entity of no instance is itself", clickTarget(scene, loose, house) == loose);
}

// A copy taken from inside a prefab instance, duplicated or pasted back, lands outside it: the
// scene does not store an instance's interior, so one landed there would be gone at the next save.
void testACopyNeverLandsInsideAnInstance() {
    std::printf("Where a copy of a part of a prefab instance lands:\n");

    Scene scene;
    const EntityId house = scene.createEntity();
    const EntityId door  = scene.createEntity();
    const EntityId knob  = scene.createEntity();
    const EntityId shelf = scene.createEntity();
    scene.add(house, PrefabInstance{});
    HierarchyOperations::setParent(scene, door, house);
    HierarchyOperations::setParent(scene, knob, door);

    // The knob's copy asks for the knob's parent, as Paste in the same world and Duplicate do.
    const EntityId knobCopyParent = copyParent(scene, door);
    check("a part's copy is not put inside the instance", !Prefab::instanceRootOf(scene, knobCopyParent));
    check("  it stands at the root", !knobCopyParent);
    check("nor under the instance root itself", !copyParent(scene, house));
    check("a copy under an ordinary parent keeps it", copyParent(scene, shelf) == shelf);
    // The one rule a create, a re-parent and a copy all ask.
    check("a part of an instance takes no new child", refusesNewChild(scene, knob));
    check("  nor does the instance root", refusesNewChild(scene, house));
    check("  an ordinary entity does", !refusesNewChild(scene, shelf));
    check("  as does the root of the scene", !refusesNewChild(scene, EntityId{}));
    scene.destroyEntity(shelf);
    check("  and goes to the root once that parent is gone", !copyParent(scene, shelf));
    check("a root's copy stays a root", !copyParent(scene, EntityId{}));
}

// A click on a toast opens the Console view its message points at, which need not be its kind's;
// the pointer in the message is written from that view, so the two cannot disagree.
void testAToastOpensTheViewItNames() {
    std::printf("Which Console view a click on a toast opens:\n");

    EditorState state;
    state.pushToast(ToastKind::Error, "vkm build failed", ConsoleView::Build);
    check("a failed build's opens the Build", state.toasts.back().opens() == ConsoleView::Build);
    check(
        "  and its message says so",
        state.toasts.back().message == "vkm build failed - see the Console's Build"
    );
    state.pushToast(ToastKind::Error, "[Script] Door", ConsoleView::Errors);
    check("  one the project caused opens the Errors", state.toasts.back().opens() == ConsoleView::Errors);
    state.pushToast(ToastKind::Warning, "vkm build is still running", ConsoleView::Build);
    check("  a warning naming the Build opens it", state.toasts.back().opens() == ConsoleView::Build);
    // An editor operation's failure is the log's to explain; Errors holds what the project caused.
    state.pushToast(ToastKind::Error, "Save failed: scene.json");
    check("one naming none opens the Log, an error too", state.toasts.back().opens() == ConsoleView::Log);
    check("  and its message is left as it was", state.toasts.back().message == "Save failed: scene.json");
    state.pushToast(ToastKind::Warning, "Stop discarded the edits made during play");
    check("  as does a warning", state.toasts.back().opens() == ConsoleView::Log);
    state.pushToast(ToastKind::Info, "Saved");
    check("  and an info one opens nothing", !state.toasts.back().opens());
}

// The repair of a refused probe as weights, found from the verdicts alone: a probe inside a wall
// captures the far room, and the fill applies them to every grid the bake and its relights make.
void testTheRepairIsABlendOfTrustedProbes() {
    std::printf("The repair of a refused probe, as weights:\n");

    // A pocket three deep: its middle blends both ends, by half each.
    const std::vector<bool> pocket = {true, false, false, false, true};
    const std::vector<ProbeFill> fills = probeFillWeights(pocket, 5, 1, 1);
    check("a fill per refused probe", fills.size() == 3);
    const auto weightOf = [](const ProbeFill& fill, uint32_t source) {
        for (const auto& [cell, weight] : fill.sources) {
            if (cell == source) return weight;
        }
        return 0.0f;
    };
    check(
        "  the cell beside each end takes that end",
        fills.size() == 3 && nearly(weightOf(fills[0], 0), 1.0f) && nearly(weightOf(fills[2], 4), 1.0f)
    );
    // Borrowing a neighbour filled in the same round would give it the left end's light.
    check(
        "  and the middle one takes each end by half, not whichever was filled first",
        fills.size() == 3 && fills[1].cell == 2 && nearly(weightOf(fills[1], 0), 0.5f)
            && nearly(weightOf(fills[1], 4), 0.5f)
    );

    // The spread must cross the whole box, or the corner opposite the one trusted probe stays
    // black - a shadow nothing casts.
    std::vector<bool> lone(64, false);
    lone[0] = true;
    check("one trusted probe reaches the whole grid", probeFillWeights(lone, 4, 4, 4).size() == 63);

    // Deeper pockets compound blends of blends; whatever the depth, a fill reads trusted probes
    // alone and keeps the light it is given.
    std::vector<bool> sparse(64, false);
    sparse[0]  = true;
    sparse[63] = true;
    bool whole = true;
    bool fromTrusted = true;
    for (const ProbeFill& fill : probeFillWeights(sparse, 4, 4, 4)) {
        float sum = 0.0f;
        for (const auto& [cell, weight] : fill.sources) {
            sum += weight;
            fromTrusted = fromTrusted && sparse[cell];
        }
        whole = whole && nearly(sum, 1.0f);
    }
    check("every fill's weights sum to one", whole);
    check("  and every source is a trusted probe", fromTrusted);

    const std::vector<bool> none(8, false);
    const std::vector<bool> all(8, true);
    check("nothing trusted, nothing to fill from", probeFillWeights(none, 2, 2, 2).empty());
    check("nothing refused, nothing to fill", probeFillWeights(all, 2, 2, 2).empty());
    // A caller bug; filling half of it would put a wall's light into a room. Read as a grid of
    // four, the second cell would have something to fill.
    const std::vector<bool> longer = {true, false, true, true, true, true, true, true};
    check("verdicts sized differently from the grid find nothing", probeFillWeights(longer, 4, 1, 1).empty());
}

// Snapping moves a drag in whole steps from where it began. Rounding the value
// itself would snap every component at the first frame: an X drag would move Y
// and Z, and an import at scale 0.01 would collapse to zero, making children NaN.
void testAGizmoSnapStepsFromWhereTheDragBegan() {
    std::printf("Gizmo snapping:\n");

    check("a drag travels in whole steps", nearly(snapTravel(0.26f, 0.1f), 0.3f));
    check("  and a zero step leaves it free", snapTravel(0.26f, 0.0f) == 0.26f);

    check(
        "a scale that began off the grid steps from where it began",
        nearly(snapScale(0.25f, 0.37f, 0.1f), 0.35f)
    );
    check(
        "  so a scale of 0.01 is not rounded to zero by holding still",
        nearly(snapScale(0.01f, 0.0101f, 0.1f), 0.01f)
    );
    check("  and a step down never reaches zero", snapScale(0.3f, 0.003f, 0.1f) >= 0.1f - 1e-4f);
    check("  nor falls below one step from above it", snapScale(0.25f, 0.02f, 0.1f) >= 0.1f - 1e-4f);
}

MeshAsset triangles(std::initializer_list<glm::vec3> corners) {
    MeshAsset mesh;
    for (const glm::vec3& corner : corners) {
        Vertex v{};
        v.position = corner;
        mesh.indices.push_back(static_cast<uint32_t>(mesh.vertices.size()));
        mesh.vertices.push_back(v);
    }
    mesh.computeAndSetBounds();
    return mesh;
}

// A room's box holds everything in it and is entered first, so picking by world box
// alone would select the arena over the crate. The box is a candidate; the
// triangles answer.
void testAPickLandsOnTrianglesNotOnABox() {
    std::printf("Picking against a mesh's triangles:\n");

    // The arena's floor at y = 0 and one wall rising to y = 3, open above.
    const MeshAsset arena = triangles({
        {-5.0f, 0.0f, -5.0f}, { 5.0f, 0.0f, -5.0f}, {-5.0f, 0.0f,  5.0f},
        { 5.0f, 0.0f, -5.0f}, { 5.0f, 0.0f,  5.0f}, {-5.0f, 0.0f,  5.0f},
        {-5.0f, 0.0f, -5.0f}, { 5.0f, 0.0f, -5.0f}, {-5.0f, 3.0f, -5.0f},
    });
    const Math::Ray down{{0.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};

    float boxT = 0.0f;
    const glm::vec3 invDown = 1.0f / down.direction;
    check(
        "the arena's box is entered at its top",
        Math::rayIntersectsAABB(down.origin, invDown, arena.bounds(), boxT) && nearly(boxT, 7.0f)
    );

    float t = 0.0f;
    check(
        "  its triangles at the floor, so a crate standing there is nearer",
        rayHitsMesh(down, glm::mat4(1.0f), arena, t) && nearly(t, 10.0f)
    );

    // Through the box's top corner, where the wall does not reach.
    const Math::Ray corner{{4.5f, 10.0f, -4.9f}, {0.0f, -1.0f, 0.0f}};
    check(
        "a ray inside the box that crosses the floor alone meets the floor",
        rayHitsMesh(corner, glm::mat4(1.0f), arena, t) && nearly(t, 10.0f)
    );

    const MeshAsset half = triangles({{-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}});
    const Math::Ray missing{{0.9f, 0.9f, 5.0f}, {0.0f, 0.0f, -1.0f}};
    check(
        "a ray through the empty half of a box hits nothing",
        !rayHitsMesh(missing, glm::mat4(1.0f), half, t)
    );

    // Twice the size and ten units away: the ray goes into mesh space, and the
    // distance comes back in world space.
    glm::mat4 placed = glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, -10.0f});
    placed = glm::scale(placed, glm::vec3(2.0f));
    const Math::Ray ahead{{-0.2f, -0.2f, 0.0f}, {0.0f, 0.0f, -1.0f}};
    check(
        "a placed mesh answers at its world distance",
        rayHitsMesh(ahead, placed, half, t) && nearly(t, 10.0f)
    );

    const Math::Ray away{{-0.2f, -0.2f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    check("  and not behind the ray's origin", !rayHitsMesh(away, placed, half, t));
}

// The default layout selects each node's tab by id, and ImGui derives a tab's id
// from its window's by an unpublished scheme (openOnTab, in chrome/dock_layout.cpp).
// So the scheme is read where ImGui defines it: if it changed, both nodes would
// silently open on the wrong tab.
void testTheDefaultLayoutNamesTabsAsImGuiDoes() {
    std::printf("The tab ids the default layout opens on:\n");

    const std::filesystem::path source =
        std::filesystem::path(VKM_ENGINE_DIR) / "modules" / "imgui" / "imgui.cpp";
    std::ifstream in(source);
    if (!in) {
        std::printf("      no ImGui source beside the engine; nothing to check\n");
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    check(
        "a docked window's tab is \"#TAB\" in the window's own id space",
        text.find("TabId = GetID(\"#TAB\");") != std::string::npos
    );
}

// A behavior field's name is its serialized key; the inspector shows it as words,
// and decides by it whether a vector is a color.
void testABehaviorFieldIsLabelledInWords() {
    std::printf("How a behavior field is labelled:\n");

    check("camelCase becomes words", fieldLabel("degreesPerSecond") == "Degrees Per Second");
    check("  one word is capitalised", fieldLabel("speed") == "Speed");
    check("  a run of capitals stays one word", fieldLabel("maxHPValue") == "Max HP Value");
    check("  and ends where a word begins", fieldLabel("HDRColor") == "HDR Color");
    check("  a digit ends a word", fieldLabel("lod2Bias") == "Lod2 Bias");
    check("  an underscore is a space", fieldLabel("jump_height") == "Jump Height");

    check("a field whose name ends in color is a color", namesAColor("tintColor") && namesAColor("color"));
    check("  and so is one in capitals", namesAColor("COLOR"));
    check("  and nothing else is", !namesAColor("colorful") && !namesAColor("velocity"));
}

// A pack is walked as a menu asks for it: a folder in it that cannot be read is a refusal said, not
// an exception out of the frame.
void testAPackWithAFolderItCannotReadIsRefused() {
    std::printf("A content pack holding a folder that cannot be read:\n");
    ScratchProject project("unreadable_pack");
    const std::filesystem::path pack = project.root() / "pack";
    std::error_code ec;
    std::filesystem::create_directories(pack / "prefabs" / "shut", ec);
    std::ofstream(pack / "prefabs" / "open.json") << "{}";
    std::filesystem::permissions(pack / "prefabs" / "shut", std::filesystem::perms::none, ec);
    // Only where the bits take reading away: on Windows they mark the folder read-only, no more.
    std::error_code shut;
    std::filesystem::directory_iterator probe(pack / "prefabs" / "shut", shut);
    if (!shut) {
        std::filesystem::permissions(pack / "prefabs" / "shut", std::filesystem::perms::owner_all, ec);
        std::printf("      (this file system keeps the folder readable - skipped)\n");
        return;
    }

    std::string error;
    int         brought = 0;
    bool        threw   = false;
    try {
        brought = ContentPack::bringInto(pack, project.root() / "into", error);
        ContentPack::prefabsIn(pack, "shut");
    } catch (const std::exception&) {
        threw = true;
    }
    std::filesystem::permissions(pack / "prefabs" / "shut", std::filesystem::perms::owner_all, ec);
    check("nothing is thrown", !threw);
    check("  and the bring-in is refused, with a reason", brought == -1 && !error.empty());
}

// Create > Effect brings the engine's effects pack in whole the first time and leaves the project's
// copies as they are after; each effect builds off the wire, and a one-off goes with the part that
// lasts longest, its scorch mark too.
void testAnEffectBringsTheEnginesPackInOnce() {
    std::printf("The engine's effects, brought into a project:\n");
    ScratchProject project("effects_pack");
    const std::filesystem::path    pack    = ContentPack::shipped("effects");
    const std::vector<std::string> effects = ContentPack::prefabsIn(pack, "effects");
    check(
        "the engine ships an explosion among its effects",
        effects.size() >= 4 && std::find(effects.begin(), effects.end(), "explosion.json") != effects.end()
    );

    int files = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(pack, ec)) {
        files += entry.is_regular_file(ec) ? 1 : 0;
    }
    std::string error;
    check("the first brings every file in", ContentPack::bringInto(pack, project.root(), error) == files);

    Scene          scene;
    ResourceManager resources;
    bool  built   = true;
    bool  offWire = true;
    for (const std::string& effect : effects) {
        const EntityId root = Prefab::instantiate(scene, resources, "prefabs/effects/" + effect);
        built   = built && static_cast<bool>(root);
        offWire = offWire && root && scene.has<NetLocal>(root);
    }
    check("  each effect builds", built);
    check("  off the wire, every end its own", offWire);

    const EntityId blast = Prefab::instantiate(scene, resources, "prefabs/effects/explosion.json");
    const ParticleEmitter* longest = scene.tryGet<ParticleEmitter>(blast);
    const EntityId scorch = HierarchyOperations::findInSelfOrDescendants<Decal>(scene, blast);
    check(
        "  the explosion goes with its longest-lived part, scorch and all",
        longest && longest->destroyWhenSpent && static_cast<bool>(scorch)
    );

    const std::filesystem::path mine = project.root() / "assets" / "effects" / "scorch.png";
    std::ofstream(mine, std::ios::trunc) << "mine";
    check("a second brings nothing", ContentPack::bringInto(pack, project.root(), error) == 0);
    std::ifstream kept(mine);
    const std::string held((std::istreambuf_iterator<char>(kept)), std::istreambuf_iterator<char>());
    check("  leaving a file the project edited as it is", held == "mine");
}

// A file dropped from outside the project is copied in where its kind's Import looks: a glTF in a
// folder of its own with the buffer and image it names, an image beside the textures. Dropped again
// it is the same copy; another file of its name takes a free one; one inside stays where it is.
void testADroppedFileIsBroughtIntoTheProject() {
    std::printf("A file dropped from outside the project:\n");

    namespace fs = std::filesystem;
    const ScratchProject project("vkm_file_drop");
    const fs::path outside = runScratch() / "vkm_file_drop_outside";
    std::error_code ec;
    fs::remove_all(outside, ec);
    fs::create_directories(outside / "maps", ec);
    const auto write = [](const fs::path& path, const std::string& text) { std::ofstream(path) << text; };
    write(
        outside / "boat.gltf",
        R"({"buffers":[{"uri":"boat.bin"}],"images":[{"uri":"maps/hull%20paint.png"}]})"
    );
    write(outside / "boat.bin", "bytes");
    write(outside / "maps" / "hull paint.png", "pixels");
    write(outside / "sail.png", "canvas");

    std::string error;
    const fs::path model = FileImport::intoProject(outside / "boat.gltf", error);
    const fs::path folder = ProjectPaths::assets() / "models" / "boat";
    check("a glTF goes into a folder of its own", model == folder / "boat.gltf" && fs::exists(model));
    check(
        "  with the buffer and the image it names",
        fs::exists(folder / "boat.bin") && fs::exists(folder / "maps" / "hull paint.png")
    );
    check("  and nothing left behind", error.empty());
    check(
        "  dropped again, it is the same copy",
        FileImport::intoProject(outside / "boat.gltf", error) == model
    );

    const fs::path image = FileImport::intoProject(outside / "sail.png", error);
    check("an image goes beside the textures", image == ProjectPaths::assets() / "textures" / "sail.png");
    write(outside / "sail.png", "another canvas");
    check(
        "  and another of its name takes a free one",
        FileImport::intoProject(outside / "sail.png", error)
            == ProjectPaths::assets() / "textures" / "sail_2.png"
    );
    check("a file inside the project stays where it is", FileImport::intoProject(image, error) == image);
    check(
        "a kind the editor does not read is refused",
        FileImport::intoProject(outside / "boat.bin", error).empty() && !error.empty()
    );

    // From anywhere: what a glTF names in a shape glTF does not is passed over, not thrown on.
    write(outside / "odd.gltf", R"({"images":[3, {"uri": 5}, {"uri": "half%zz.png"}], "buffers": "x"})");
    bool threw = false;
    fs::path odd;
    error.clear();
    try {
        odd = FileImport::intoProject(outside / "odd.gltf", error);
    } catch (...) {
        threw = true;
    }
    check("a glTF naming its files oddly is still brought in", !threw && !odd.empty());
    check("  and says what it could not bring", !error.empty());
    write(outside / "broken.gltf", "{ not json");
    error.clear();
    const fs::path broken = FileImport::intoProject(outside / "broken.gltf", error);
    check("  as is one that does not parse, alone", !broken.empty());
    fs::remove_all(outside, ec);
}

} // namespace

void runAuthoringTests() {
    testTheRepairIsABlendOfTrustedProbes();
    testAClickSelectsThePlacedThingBeforeItsParts();
    testACopyNeverLandsInsideAnInstance();
    testAToastOpensTheViewItNames();
    testTheConsoleFollowsTheLog();
    testAUIRectResizesFromItsHeldSide();
    testAGizmoSnapStepsFromWhereTheDragBegan();
    testAPickLandsOnTrianglesNotOnABox();
    testTheDefaultLayoutNamesTabsAsImGuiDoes();
    testABehaviorFieldIsLabelledInWords();
    testAnEffectBringsTheEnginesPackInOnce();
    testAPackWithAFolderItCannotReadIsRefused();
    testADroppedFileIsBroughtIntoTheProject();
}
