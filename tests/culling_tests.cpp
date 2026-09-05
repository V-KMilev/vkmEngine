#include "support.h"

#include "core/math/frustum.h"

namespace {

// A camera at the origin looking down -Z, which is the engine's forward.
glm::mat4 lookingForward(float fovY = glm::radians(60.0f), float zNear = 0.1f, float zFar = 100.0f) {
    const glm::mat4 projection = glm::perspective(fovY, 16.0f / 9.0f, zNear, zFar);
    const glm::mat4 view       = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
                                             glm::vec3(0.0f, 1.0f, 0.0f));
    return projection * view;
}

glm::vec3 boxMin(const glm::vec3& at, float half) { return at - glm::vec3(half); }
glm::vec3 boxMax(const glm::vec3& at, float half) { return at + glm::vec3(half); }

bool visible(const Math::Frustum& f, const glm::vec3& at, float half = 0.5f) {
    return Math::frustumIntersectsAABB(f, boxMin(at, half), boxMax(at, half));
}

// Culling is the one stage whose failure is invisible in the only way that
// matters: a wrong answer deletes geometry that was there, or draws geometry
// that was not, and neither reports anything. Nothing in the tree tested it.
void testTheFrustumKeepsWhatIsInFrontOfIt() {
    std::printf("What a camera can and cannot see:\n");

    const Math::Frustum f = Math::extractFrustum(lookingForward());

    check("a box straight ahead is visible", visible(f, {0.0f, 0.0f, -10.0f}));
    check("  and one behind the camera is not", !visible(f, {0.0f, 0.0f, 10.0f}));
    check("  nor one past the far plane", !visible(f, {0.0f, 0.0f, -200.0f}));
    check("  nor one nearer than the near plane", !visible(f, {0.0f, 0.0f, -0.01f}, 0.001f));

    // Forward is -Z. A frustum built from a +Z-forward assumption passes the
    // first check by symmetry and fails this one, which is the whole point of
    // asserting both directions rather than only the visible case.
    check("far to the left is outside", !visible(f, {-100.0f, 0.0f, -10.0f}));
    check("far to the right is outside", !visible(f, {100.0f, 0.0f, -10.0f}));
    check("far above is outside", !visible(f, {0.0f, 100.0f, -10.0f}));
    check("far below is outside", !visible(f, {0.0f, -100.0f, -10.0f}));
}

void testABoxOnTheEdgeCountsAsVisible() {
    std::printf("A box the frustum only partly contains:\n");

    const Math::Frustum f = Math::extractFrustum(lookingForward());

    // Straddling a plane is visible, not culled: the test is intersects, and a
    // wall the camera is standing in the middle of must still be drawn.
    check("a box straddling the near plane is visible",
          Math::frustumIntersectsAABB(f, glm::vec3(-1.0f, -1.0f, -0.5f),
                                         glm::vec3( 1.0f,  1.0f,  0.5f)));

    // Huge and centred on the camera: every plane has the centre behind it, and
    // only the half-extent term keeps it in. This is the case a signed-distance
    // test without the radius term gets wrong.
    check("a box swallowing the camera is visible",
          Math::frustumIntersectsAABB(f, glm::vec3(-500.0f), glm::vec3(500.0f)));

    // A degenerate box is a point, and a point in front is in.
    check("a zero-size box in front is visible", visible(f, {0.0f, 0.0f, -5.0f}, 0.0f));
}

void testTheFrustumNarrowsWithTheFieldOfView() {
    std::printf("What the field of view actually changes:\n");

    const Math::Frustum wide   = Math::extractFrustum(lookingForward(glm::radians(100.0f)));
    const Math::Frustum narrow = Math::extractFrustum(lookingForward(glm::radians(20.0f)));

    // A box off to the side that a wide lens keeps and a narrow one drops. Both
    // are the same world; only the frustum changed.
    const glm::vec3 offToTheSide{5.0f, 0.0f, -6.0f};
    check("a wide field of view keeps a box off to the side", visible(wide, offToTheSide));
    check("  and a narrow one drops it", !visible(narrow, offToTheSide));
    check("  while both keep what is straight ahead",
          visible(wide, {0.0f, 0.0f, -6.0f}) && visible(narrow, {0.0f, 0.0f, -6.0f}));
}

void testEveryPlaneNormalIsAUnitVector() {
    std::printf("The planes the extraction produced:\n");

    const Math::Frustum f = Math::extractFrustum(lookingForward());

    bool normalized = true;
    bool absMatches = true;
    for (int i = 0; i < 6; ++i) {
        normalized &= nearly(glm::length(f.normals[i]), 1.0f);
        absMatches &= nearly(f.absNormals[i].x, std::abs(f.normals[i].x))
                   && nearly(f.absNormals[i].y, std::abs(f.normals[i].y))
                   && nearly(f.absNormals[i].z, std::abs(f.normals[i].z));
    }
    // The AABB test multiplies the half-extent by absNormals and compares
    // against a distance in world units. Both only mean anything if the plane
    // is normalized, and an unnormalized one scales the radius silently.
    check("every plane normal is unit length", normalized);
    check("  and absNormals is its component-wise absolute", absMatches);
}

} // namespace

void runCullingTests() {
    testTheFrustumKeepsWhatIsInFrontOfIt();
    testABoxOnTheEdgeCountsAsVisible();
    testTheFrustumNarrowsWithTheFieldOfView();
    testEveryPlaneNormalIsAUnitVector();
}
