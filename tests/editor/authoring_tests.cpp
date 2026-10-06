#include "support.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>

#include "system/render/irradiance_dilation.h"

#include "overlays/gizmo_snap.h"
#include "overlays/mesh_pick.h"
#include "ui/field_label.h"

namespace {

// What the editor computes and then trusts unchecked: a badly repaired probe grid
// is a glowing wall, a bad snap a drifting transform, a bad pick the room instead
// of the crate in it.

// A probe grid as the bake reads it back: every cell the same, alpha saying trusted.
ProbeGridSH makeGrid(uint32_t x, uint32_t y, uint32_t z, float value) {
    ProbeGridSH sh;
    for (std::vector<glm::vec4>& grid : sh) {
        grid.assign(static_cast<size_t>(x) * y * z, glm::vec4(value, value, value, 1.0f));
    }
    return sh;
}

void refuse(ProbeGridSH& sh, size_t cell) {
    for (std::vector<glm::vec4>& grid : sh) grid[cell] = glm::vec4(0.0f);
}

void testRepairingTheProbesABakeRefused() {
    std::printf("A probe grid with probes the bake would not trust:\n");

    // A grid changed with nothing refused would be a bake blurring itself every run.
    ProbeGridSH clean = makeGrid(3, 1, 1, 2.0f);
    check("a grid the bake trusted entirely reports nothing refused", dilateProbeGrid(clean, 3, 1, 1) == 0);
    check("  and comes back untouched", nearly(clean[0][1].x, 2.0f) && nearly(clean[3][1].x, 2.0f));

    // One probe inside a wall, between two that are not: its own capture - the room
    // on the far side - must disappear.
    ProbeGridSH one = makeGrid(3, 1, 1, 4.0f);
    refuse(one, 1);
    check("one refused probe is counted", dilateProbeGrid(one, 3, 1, 1) == 1);
    check(
        "  and every coefficient of it comes from the probes beside it",
        nearly(one[0][1].x, 4.0f)
            && nearly(one[1][1].y, 4.0f)
            && nearly(one[2][1].z, 4.0f)
            && nearly(one[3][1].x, 4.0f)
    );
    check("  leaving no verdict behind for the sampler to trip over", nearly(one[0][1].w, 1.0f));

    // A pocket three deep, a different light at each end. Borrowing a neighbour filled
    // in the same round would give the middle the left end's light, not the average.
    ProbeGridSH pocket = makeGrid(5, 1, 1, 0.0f);
    for (int c = 0; c < 4; ++c) {
        pocket[c][0] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        pocket[c][4] = glm::vec4(9.0f, 9.0f, 9.0f, 1.0f);
    }
    refuse(pocket, 1);
    refuse(pocket, 2);
    refuse(pocket, 3);
    check("a pocket three deep is filled from its rim inwards", dilateProbeGrid(pocket, 5, 1, 1) == 3);
    check(
        "  the cell beside each end takes that end",
        nearly(pocket[0][1].x, 1.0f) && nearly(pocket[0][3].x, 9.0f)
    );
    check(
        "  and the one between them takes both, not whichever was filled first",
        nearly(pocket[0][2].x, 5.0f)
    );

    // The spread must cross the whole box, or the corner opposite the one trusted
    // probe stays black - a shadow nothing casts.
    ProbeGridSH sparse = makeGrid(4, 4, 4, 0.0f);
    for (int c = 0; c < 4; ++c) sparse[c][0] = glm::vec4(3.0f, 3.0f, 3.0f, 1.0f);
    for (size_t cell = 1; cell < 64; ++cell) refuse(sparse, cell);
    check("one trusted probe is enough to reach the whole grid", dilateProbeGrid(sparse, 4, 4, 4) == 63);
    bool reached = true;
    for (size_t cell = 0; cell < 64; ++cell) {
        reached = reached && nearly(sparse[0][cell].x, 3.0f) && nearly(sparse[0][cell].w, 1.0f);
    }
    check("  including the corner furthest from it", reached);

    // Nothing to repair from: the grid comes back as it went in, since the baker then
    // refuses the volume, and a half-filled grid shipped would be worse than the leak.
    ProbeGridSH lost = makeGrid(2, 2, 2, 7.0f);
    for (size_t cell = 0; cell < 8; ++cell) refuse(lost, cell);
    check("a volume where nothing was trusted says so", dilateProbeGrid(lost, 2, 2, 2) == 8);
    bool untouched = true;
    for (size_t cell = 0; cell < 8; ++cell) untouched = untouched && nearly(lost[0][cell].w, 0.0f);
    check("  and is left for the caller to refuse rather than guessed at", untouched);

    // A grid that does not match its stated resolution is a caller bug; writing half
    // of it would put a wall's light into a room.
    ProbeGridSH wrong = makeGrid(3, 1, 1, 5.0f);
    refuse(wrong, 1);
    check(
        "a grid sized differently from the resolution given is left alone",
        dilateProbeGrid(wrong, 4, 1, 1) == 0 && nearly(wrong[0][1].w, 0.0f)
    );
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
// and decides by it whether a vector is a colour.
void testABehaviorFieldIsLabelledInWords() {
    std::printf("How a behavior field is labelled:\n");

    check("camelCase becomes words", fieldLabel("degreesPerSecond") == "Degrees Per Second");
    check("  one word is capitalised", fieldLabel("speed") == "Speed");
    check("  a run of capitals stays one word", fieldLabel("maxHPValue") == "Max HP Value");
    check("  and ends where a word begins", fieldLabel("HDRColor") == "HDR Color");
    check("  a digit ends a word", fieldLabel("lod2Bias") == "Lod2 Bias");
    check("  an underscore is a space", fieldLabel("jump_height") == "Jump Height");

    check("a name ending in color is a colour", namesAColor("tintColor") && namesAColor("color"));
    check("  so is colour, in any case", namesAColor("glowColour") && namesAColor("COLOR"));
    check("  and nothing else is", !namesAColor("colorful") && !namesAColor("velocity"));
}

} // namespace

void runAuthoringTests() {
    testRepairingTheProbesABakeRefused();
    testAGizmoSnapStepsFromWhereTheDragBegan();
    testAPickLandsOnTrianglesNotOnABox();
    testTheDefaultLayoutNamesTabsAsImGuiDoes();
    testABehaviorFieldIsLabelledInWords();
}
