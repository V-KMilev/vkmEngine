#include "support.h"

#include <cstring>
#include <string>
#include <vector>

#include "ecs/component/core/world_transform.h"
#include "ecs/hierarchy_operations.h"
#include "system/hierarchy/hierarchy_system.h"
#include "io/scene/scene_serializer.h"

namespace {

// The entity graph under UI nesting, an imported model's nodes and parented rigidbodies.

EntityId named(Scene& scene, const char* name, const glm::vec3& at = glm::vec3(0.0f)) {
    const EntityId id = scene.createEntity();
    scene.add(id, makeName(name));
    Transform t;
    t.position = at;
    scene.add<Transform>(id, std::move(t));
    return id;
}

void testParentingAndDetaching() {
    std::printf("What setParent does to both ends:\n");

    Scene scene;
    const EntityId root = named(scene, "Root");
    const EntityId a    = named(scene, "A");
    const EntityId b    = named(scene, "B");

    HierarchyOperations::setParent(scene, a, root);
    HierarchyOperations::setParent(scene, b, root);

    check("the parent gains a Hierarchy", scene.has<Hierarchy>(root));
    check("  and so does the child", scene.has<Hierarchy>(a));
    // Pre-seeded so the per-frame resolve never mutates the component graph, which
    // lets it parallelise per tree level.
    check(
        "both are given a WorldTransform up front",
        scene.has<WorldTransform>(root) && scene.has<WorldTransform>(a)
    );
    check("the parent sees both children", childrenOf(scene, root) == 2);
    check("  and a child knows its parent", scene.get<Hierarchy>(a).parent == root);

    // Detaching must relink the siblings around the hole; a broken list quietly
    // loses whole subtrees from the walk.
    HierarchyOperations::removeFromParent(scene, a);
    check("detaching leaves the parent with the rest", childrenOf(scene, root) == 1);
    check("  and the sibling is still reachable", scene.get<Hierarchy>(root).firstChild == b);
    // A detached leaf loses both components, so a caller cannot assume it still has
    // a WorldTransform to read.
    check("  while a detached leaf loses its Hierarchy entirely", !scene.has<Hierarchy>(a));
    check("  and the WorldTransform that came with it", !scene.has<WorldTransform>(a));

    // A detached branch keeps both: it still has children to link.
    const EntityId branch = named(scene, "Branch");
    const EntityId leaf   = named(scene, "Leaf");
    HierarchyOperations::setParent(scene, branch, root);
    HierarchyOperations::setParent(scene, leaf, branch);
    HierarchyOperations::removeFromParent(scene, branch);
    check(
        "a detached branch keeps its Hierarchy, having children still",
        scene.has<Hierarchy>(branch) && childrenOf(scene, branch) == 1
    );

    // Re-parenting detaches first, rather than leaving the entity in two child lists.
    const EntityId other = named(scene, "Other");
    HierarchyOperations::setParent(scene, b, other);
    check(
        "re-parenting detaches from the old parent first",
        childrenOf(scene, root) == 0 && childrenOf(scene, other) == 1
    );
}

void testACycleIsRefused() {
    std::printf("A parenting that would close a ring:\n");

    Scene scene;
    const EntityId a = named(scene, "A");
    const EntityId b = named(scene, "B");
    const EntityId c = named(scene, "C");
    HierarchyOperations::setParent(scene, b, a);
    HierarchyOperations::setParent(scene, c, b);

    check("a descendant is recognised as one", HierarchyOperations::isAncestorOf(scene, a, c));
    check("  and an unrelated entity is not", !HierarchyOperations::isAncestorOf(scene, c, a));

    // Parenting to a descendant would close a ring, and a walk would run to its bound.
    HierarchyOperations::setParent(scene, a, c);
    check("parenting to a descendant is refused", !scene.get<Hierarchy>(a).parent);
    check(
        "  and leaves the existing chain intact",
        scene.get<Hierarchy>(c).parent == b && scene.get<Hierarchy>(b).parent == a
    );
}

void testWorldTransformsResolveThroughTheChain() {
    std::printf("Where a child ends up:\n");

    Scene scene;
    const EntityId root  = named(scene, "Root",  {10.0f, 0.0f, 0.0f});
    const EntityId child = named(scene, "Child", { 0.0f, 5.0f, 0.0f});
    const EntityId grand = named(scene, "Grand", { 0.0f, 0.0f, 2.0f});
    HierarchyOperations::setParent(scene, child, root);
    HierarchyOperations::setParent(scene, grand, child);

    TestFrame frame(scene);
    HierarchySystem hierarchy;
    hierarchy.update(frame.ctx);

    const glm::vec3 childWorld = glm::vec3(scene.get<WorldTransform>(child).model[3]);
    const glm::vec3 grandWorld = glm::vec3(scene.get<WorldTransform>(grand).model[3]);
    check(
        "a child's world position is its parent's plus its own",
        sameDirection(childWorld, {10.0f, 5.0f, 0.0f})
    );
    check("  and the chain composes to the grandchild", sameDirection(grandWorld, {10.0f, 5.0f, 2.0f}));

    // Resolved unconditionally: a dirty flag is only correct if every Transform
    // writer, engine or game, remembers to set it.
    scene.get<Transform>(root).position = {0.0f, 0.0f, 0.0f};
    hierarchy.update(frame.ctx);
    check(
        "moving the parent moves the subtree with no flag set",
        sameDirection(glm::vec3(scene.get<WorldTransform>(grand).model[3]), {0.0f, 5.0f, 2.0f})
    );

    // The world-pose rule: WorldTransform if present, else the local Transform.
    // computeWorldMatrix walks the chain instead and must agree.
    const glm::mat4 walked = HierarchyOperations::computeWorldMatrix(scene, grand);
    check(
        "walking the ancestors agrees with the resolved matrix",
        sameDirection(glm::vec3(walked[3]), glm::vec3(scene.get<WorldTransform>(grand).model[3]))
    );
}

void testDestroyingASubtree() {
    std::printf("Destroying a parent:\n");

    Scene scene;
    const EntityId root  = named(scene, "Root");
    const EntityId child = named(scene, "Child");
    const EntityId grand = named(scene, "Grand");
    const EntityId other = named(scene, "Other");
    HierarchyOperations::setParent(scene, child, root);
    HierarchyOperations::setParent(scene, grand, child);

    HierarchyOperations::destroyHierarchy(scene, root);
    check("the whole subtree goes", !scene.isAlive(root) && !scene.isAlive(child) && !scene.isAlive(grand));
    check("  and nothing else does", scene.isAlive(other));
}

bool samePose(const glm::mat4& a, const glm::mat4& b) {
    for (int column = 0; column < 4; ++column) {
        if (glm::length(a[column] - b[column]) >= 1e-3f) return false;
    }
    return true;
}

void testDestroyingAParentLeavesItsChildrenInPlace() {
    std::printf("Destroying a parent without its subtree:\n");

    Scene scene;
    const EntityId root   = named(scene, "Root",   {10.0f, 0.0f, 0.0f});
    const EntityId doomed = named(scene, "Doomed", { 0.0f, 5.0f, 0.0f});
    const EntityId child  = named(scene, "Child",  { 1.0f, 0.0f, 0.0f});
    const EntityId grand  = named(scene, "Grand",  { 0.0f, 0.0f, 1.0f});
    scene.get<Transform>(doomed).rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 1, 0));
    scene.get<Transform>(doomed).scale    = glm::vec3(2.0f);
    HierarchyOperations::setParent(scene, doomed, root);
    HierarchyOperations::setParent(scene, child, doomed);
    HierarchyOperations::setParent(scene, grand, child);

    const glm::mat4 childBefore = HierarchyOperations::computeWorldMatrix(scene, child);
    const glm::mat4 grandBefore = HierarchyOperations::computeWorldMatrix(scene, grand);
    scene.destroyEntity(doomed);

    check(
        "the orphan is taken in by its grandparent",
        scene.get<Hierarchy>(child).parent == root && childrenOf(scene, root) == 1
    );
    // Its local Transform was relative to the destroyed parent; kept as is, the
    // child would jump by everything that parent contributed.
    check(
        "  and stays where it stood",
        samePose(HierarchyOperations::computeWorldMatrix(scene, child), childBefore)
    );
    check(
        "  carrying its own subtree along unmoved",
        samePose(HierarchyOperations::computeWorldMatrix(scene, grand), grandBefore)
    );

    TestFrame frame(scene);
    HierarchySystem hierarchy;
    hierarchy.update(frame.ctx);
    check(
        "  which is what the resolve draws it at",
        samePose(scene.get<WorldTransform>(grand).model, grandBefore)
    );

    // With no grandparent the children become roots, and a leaf root loses its
    // Hierarchy and WorldTransform, as with removeFromParent.
    const EntityId lone = named(scene, "Lone", {0.0f, 0.0f, 4.0f});
    const EntityId leaf = named(scene, "Leaf", {0.0f, 2.0f, 0.0f});
    HierarchyOperations::setParent(scene, leaf, lone);
    scene.destroyEntity(lone);
    check(
        "a root's destroyed children stay where they stood",
        sameDirection(scene.get<Transform>(leaf).position, {0.0f, 2.0f, 4.0f})
    );
    check(
        "  and a leaf left with no relatives loses its Hierarchy",
        !scene.has<Hierarchy>(leaf) && !scene.has<WorldTransform>(leaf)
    );
}

void testTheResolveAgreesWithTheWalk() {
    std::printf("Resolving a branching tree a level at a time:\n");

    Scene scene;
    const EntityId root  = named(scene, "Root",  {1.0f, 0.0f, 0.0f});
    const EntityId arm   = named(scene, "Arm",   {0.0f, 1.0f, 0.0f});
    const EntityId hand  = named(scene, "Hand",  {0.0f, 0.0f, 1.0f});
    const EntityId thumb = named(scene, "Thumb", {0.0f, 0.0f, 2.0f});
    const EntityId other = named(scene, "Other", {5.0f, 0.0f, 0.0f});
    const EntityId below = named(scene, "Below", {3.0f, 0.0f, 0.0f});
    // An entity with no Transform places nothing: children compose onto the matrix
    // it has, and the upward walk stops there too.
    const EntityId group = scene.createEntity();
    HierarchyOperations::setParent(scene, arm, root);
    HierarchyOperations::setParent(scene, hand, arm);
    HierarchyOperations::setParent(scene, thumb, arm);
    HierarchyOperations::setParent(scene, group, root);
    HierarchyOperations::setParent(scene, below, group);
    HierarchyOperations::setParent(scene, other, hand);
    scene.get<Transform>(root).rotation = glm::angleAxis(0.5f, glm::vec3(0, 0, 1));

    TestFrame frame(scene);
    HierarchySystem hierarchy;
    hierarchy.update(frame.ctx);

    bool agree = true;
    for (const EntityId id : {root, arm, hand, thumb, other, below}) {
        agree = agree && samePose(
            scene.get<WorldTransform>(id).model,
            HierarchyOperations::computeWorldMatrix(scene, id)
        );
    }
    check("every level resolves to what walking its ancestors composes", agree);
    check(
        "  and under a Transform-less node a child is placed by its own Transform",
        sameDirection(glm::vec3(scene.get<WorldTransform>(below).model[3]), {3.0f, 0.0f, 0.0f})
    );
}

void testAChainIsNeverMadeDeeperThanTheWalksGo() {
    std::printf("A chain grown past the depth the engine supports:\n");

    using HierarchyOperations::MAX_DEPTH;
    Scene scene;
    std::vector<EntityId> chain;
    for (uint32_t i = 0; i < MAX_DEPTH + 2; ++i) {
        // A metre per link, so where the resolve stopped reads as a distance.
        chain.push_back(named(scene, "Link", {0.0f, 1.0f, 0.0f}));
        if (i > 0) HierarchyOperations::setParent(scene, chain[i], chain[i - 1]);
    }
    const auto parentOf = [&](EntityId id) {
        const Hierarchy* node = scene.tryGet<Hierarchy>(id);
        return node ? node->parent : EntityId{};
    };
    check("a chain grows to MAX_DEPTH entities", parentOf(chain[MAX_DEPTH - 1]) == chain[MAX_DEPTH - 2]);
    check("  and a link past it is refused", !parentOf(chain[MAX_DEPTH]));
    check("  which goes on as a chain of its own", parentOf(chain[MAX_DEPTH + 1]) == chain[MAX_DEPTH]);
    HierarchyOperations::setParent(scene, chain[MAX_DEPTH], chain[MAX_DEPTH - 2]);
    check("a subtree that would end past it is refused too", !parentOf(chain[MAX_DEPTH]));
    HierarchyOperations::setParent(scene, chain[MAX_DEPTH], chain[MAX_DEPTH - 3]);
    check("  and taken where it ends at it", parentOf(chain[MAX_DEPTH]) == chain[MAX_DEPTH - 3]);
    HierarchyOperations::setParent(scene, chain.front(), chain[MAX_DEPTH - 1]);
    check("parenting the top of the chain under its bottom is refused", !parentOf(chain.front()));

    TestFrame frame(scene);
    HierarchySystem hierarchy;
    hierarchy.update(frame.ctx);
    check(
        "the deepest link resolves through its whole chain",
        nearly(scene.get<WorldTransform>(chain[MAX_DEPTH - 1]).model[3].y, static_cast<float>(MAX_DEPTH))
    );
}

// A delete means "all of this goes", however deep: a tail left alive is a subtree the outliner's
// selection no longer shows.
void testDestroyingTheDeepestChainGoesWhole() {
    std::printf("Destroying a chain as deep as a hierarchy goes:\n");

    Scene scene;
    const EntityId holder = named(scene, "Holder");
    std::vector<EntityId> chain;
    for (uint32_t i = 0; i + 1 < HierarchyOperations::MAX_DEPTH; ++i) {
        chain.push_back(named(scene, "Link"));
        HierarchyOperations::setParent(scene, chain[i], i > 0 ? chain[i - 1] : holder);
    }

    HierarchyOperations::destroyHierarchy(scene, chain.front());
    bool allGone = true;
    for (const EntityId link : chain) allGone = allGone && !scene.isAlive(link);
    check("every link goes", allGone);
    check("  the entity above it stays", scene.isAlive(holder));
    const Hierarchy* above = scene.tryGet<Hierarchy>(holder);
    check("  and no longer names a child", !above || !above->firstChild);
}

std::string childNames(const Scene& scene, EntityId parent) {
    std::string names;
    HierarchyOperations::forEachChild(scene, parent, [&](EntityId child) {
        if (!names.empty()) names += ' ';
        names += scene.get<Name>(child).value;
    });
    return names;
}

EntityId findNamed(const Scene& scene, const char* name) {
    EntityId found{};
    scene.forEachEntity([&](EntityId id) {
        const Name* held = scene.tryGet<Name>(id);
        if (held && std::strcmp(held->value, name) == 0) found = id;
    });
    return found;
}

// Siblings walk in attach order, which for UI is paint order (last parented on top).
// A load re-attaches in file order, so the saver must write attach order - slot
// order differs as soon as anything is parented out of creation order.
void testSiblingsKeepTheOrderTheyWereAttachedIn() {
    std::printf("The order a parent's children are walked in:\n");

    Scene scene;
    const EntityId root = named(scene, "Root");
    const EntityId a    = named(scene, "A");
    const EntityId b    = named(scene, "B");
    const EntityId c    = named(scene, "C");
    HierarchyOperations::setParent(scene, a, root);
    HierarchyOperations::setParent(scene, b, root);
    HierarchyOperations::setParent(scene, c, root);
    check("three children are walked in the order they were attached", childNames(scene, root) == "A B C");

    HierarchyOperations::removeFromParent(scene, c);
    HierarchyOperations::setParent(scene, c, root);
    check("  one detached from the end and attached again is still last", childNames(scene, root) == "A B C");
    HierarchyOperations::setParent(scene, a, root);
    check("  and one attached again from the front goes to the end", childNames(scene, root) == "B C A");

    const EntityId d = named(scene, "D");
    HierarchyOperations::setParent(scene, d, root);
    check("  where the next one attached follows it", childNames(scene, root) == "B C A D");

    ResourceManager resources;
    const std::string document = SceneSerializer::saveToString(scene, resources);
    Scene back;
    ResourceManager backResources;
    check("the scene saves and loads back", SceneSerializer::loadFromString(document, back, backResources));
    const EntityId loadedRoot = findNamed(back, "Root");
    check(
        "  and the reloaded parent walks its children in the same order",
        loadedRoot && childNames(back, loadedRoot) == "B C A D"
    );

    const EntityId shelf  = named(scene, "Shelf");
    const EntityId first  = named(scene, "First");
    const EntityId box    = named(scene, "Box");
    const EntityId last   = named(scene, "Last");
    const EntityId inner1 = named(scene, "X");
    const EntityId inner2 = named(scene, "Y");
    HierarchyOperations::setParent(scene, first, shelf);
    HierarchyOperations::setParent(scene, box, shelf);
    HierarchyOperations::setParent(scene, last, shelf);
    HierarchyOperations::setParent(scene, inner1, box);
    HierarchyOperations::setParent(scene, inner2, box);
    scene.destroyEntity(box);
    check(
        "a destroyed entity's children take its place among its siblings",
        childNames(scene, shelf) == "First X Y Last"
    );

    HierarchyOperations::setParent(scene, last, shelf, first);
    check(
        "  and one attached in front of a sibling goes there",
        childNames(scene, shelf) == "Last First X Y"
    );
}

// A search through a subtree wider than any fixed cap is bounded by the scene, as
// collecting it is, and finds what is at its end.
void testAWideSubtreeIsSearchedWhole() {
    std::printf("A search through five thousand children:\n");

    Scene scene;
    const EntityId root = named(scene, "Root");
    EntityId lastChild{};
    for (int i = 0; i < 5000; ++i) {
        lastChild = scene.createEntity();
        HierarchyOperations::setParent(scene, lastChild, root);
    }
    // A level down, so the search has queued every child before reaching it.
    const EntityId deep = scene.createEntity();
    HierarchyOperations::setParent(scene, deep, lastChild);
    const EntityId found = HierarchyOperations::findInSelfOrDescendantsIf(
        scene,
        root,
        [&](EntityId id) { return id == deep; }
    );
    check("the grandchild under the last child is found", found == deep);
}

// A behavior finds the parts its scene gave it by name: the nearest wins, and a name
// is matched whole.
void testAPartIsFoundByItsName() {
    std::printf("A part found by its name:\n");

    Scene scene;
    const EntityId root  = named(scene, "Table");
    const EntityId rim   = named(scene, "Rim");
    const EntityId leg   = named(scene, "Leg");
    const EntityId inner = named(scene, "Rim");
    HierarchyOperations::setParent(scene, leg, root);
    HierarchyOperations::setParent(scene, inner, leg);
    HierarchyOperations::setParent(scene, rim, root);

    check("a child is found by its name", HierarchyOperations::findNamed(scene, root, "Leg") == leg);
    check("  the shallower of two with one name", HierarchyOperations::findNamed(scene, root, "Rim") == rim);
    check("  the root itself, by its own", HierarchyOperations::findNamed(scene, root, "Table") == root);
    check("a name is matched whole, not by its start", !HierarchyOperations::findNamed(scene, root, "Ri"));
    check("  and one outside the subtree is not found", !HierarchyOperations::findNamed(scene, leg, "Table"));
}

// Links not written by HierarchyOperations - a damaged file, a bug - can close a
// sibling list into a ring; every walk across one must end, and in the same place.
void testASiblingRingEndsEveryWalk() {
    std::printf("A sibling list that loops:\n");

    Scene scene;
    const EntityId root = named(scene, "Root");
    const EntityId a    = named(scene, "A");
    const EntityId b    = named(scene, "B");
    HierarchyOperations::setParent(scene, a, root);
    HierarchyOperations::setParent(scene, b, root);
    scene.get<Hierarchy>(b).nextSibling = a;

    size_t visits = 0;
    HierarchyOperations::forEachChild(scene, root, [&](EntityId) { ++visits; });
    check("walking the children ends", visits <= scene.entityCount());
    check("  collecting the subtree refuses it", HierarchyOperations::collectSubtree(scene, root).empty());

    TestFrame frame(scene);
    HierarchySystem hierarchy;
    hierarchy.update(frame.ctx);
    check("  the resolve ends", true);

    scene.destroyEntity(root);
    check("  and destroying the parent ends", !scene.isAlive(root));
}

// A stale id is a caller's mistake only an assert catches; without asserts a parent
// link through it lands on an empty slot, or on the entity that takes it next.
void testAStaleIdIsNotParented() {
    std::printf("Parenting through a stale id:\n");
#ifndef NDEBUG
    std::printf("  (asserted in this build; a VKM_ASSERTS=OFF build checks what happens without)\n");
#else
    Scene scene;
    const EntityId root  = named(scene, "Root");
    const EntityId stale = named(scene, "Gone");
    scene.destroyEntity(stale);

    HierarchyOperations::setParent(scene, stale, root);
    check("the parent is left without a link to it", !scene.has<Hierarchy>(root));
    const EntityId reused = scene.createEntity();
    check(
        "  and the entity that takes the slot inherits no link",
        reused.slot() == stale.slot() && !scene.has<Hierarchy>(reused)
    );
#endif
}

// The downward search reuses one queue per thread, and a match may search again:
// the inner search must not walk, or push onto, the outer one's queue.
void testASearchInsideASearchKeepsItsOwnQueue() {
    std::printf("A search whose question is another search:\n");

    Scene scene;
    const EntityId root   = named(scene, "Root");
    const EntityId a      = named(scene, "A");
    const EntityId a1     = named(scene, "A1");
    const EntityId b      = named(scene, "B");
    const EntityId b1     = named(scene, "B1");
    const EntityId target = named(scene, "Target");
    HierarchyOperations::setParent(scene, a, root);
    HierarchyOperations::setParent(scene, a1, a);
    HierarchyOperations::setParent(scene, b, root);
    HierarchyOperations::setParent(scene, b1, b);
    HierarchyOperations::setParent(scene, target, b1);

    const auto holdsTarget = [&](EntityId from) {
        return HierarchyOperations::findInSelfOrDescendantsIf(
            scene,
            from,
            [&](EntityId id) { return id == target; }
        ) == target;
    };
    const EntityId found = HierarchyOperations::findInSelfOrDescendantsIf(
        scene,
        root,
        [&](EntityId id) { return id != root && holdsTarget(id); }
    );
    check("the nearest entity holding the target is found", found == b);

    const EntityId again = HierarchyOperations::findInSelfOrDescendantsIf(
        scene,
        root,
        [&](EntityId id) { return id == target; }
    );
    check("  and the next search starts from an empty queue", again == target);
}

} // namespace

void runHierarchyTests() {
    testParentingAndDetaching();
    testACycleIsRefused();
    testWorldTransformsResolveThroughTheChain();
    testDestroyingASubtree();
    testDestroyingAParentLeavesItsChildrenInPlace();
    testTheResolveAgreesWithTheWalk();
    testAChainIsNeverMadeDeeperThanTheWalksGo();
    testDestroyingTheDeepestChainGoesWhole();
    testASearchInsideASearchKeepsItsOwnQueue();
    testSiblingsKeepTheOrderTheyWereAttachedIn();
    testAWideSubtreeIsSearchedWhole();
    testAPartIsFoundByItsName();
    testASiblingRingEndsEveryWalk();
    testAStaleIdIsNotParented();
}
