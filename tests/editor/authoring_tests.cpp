#include "support.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>

#include "system/render/probe_fill.h"

#include "overlays/gizmo_snap.h"
#include "overlays/mesh_pick.h"
#include "ui/field_label.h"

namespace {

// What the bake and the editor compute and then trust unchecked: a badly repaired probe grid
// is a glowing wall, a bad snap a drifting transform, a bad pick the room instead
// of the crate in it.

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
    check("  and so is one in capitals", namesAColor("COLOR"));
    check("  and nothing else is", !namesAColor("colorful") && !namesAColor("velocity"));
}

} // namespace

void runAuthoringTests() {
    testTheRepairIsABlendOfTrustedProbes();
    testAGizmoSnapStepsFromWhereTheDragBegan();
    testAPickLandsOnTrianglesNotOnABox();
    testTheDefaultLayoutNamesTabsAsImGuiDoes();
    testABehaviorFieldIsLabelledInWords();
}
