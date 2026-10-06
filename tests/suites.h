#pragma once

// The one list of suites: it drives the declarations below and main.cpp's dispatch table.
// A row is (command-line name, entry point, one-line subject). Adding a suite means
// tests/<area>/<name>_tests.cpp, a row here, and its name in docs/reference/building.md.
//
// Rows run in this order: foundations first, so a failure in the bit stream is read
// before the failures it causes in the session that rides on it.
//
// docs_tests.cpp checks that tests/ holds a file per row and that
// docs/reference/building.md names the same set.
#define VKM_TEST_SUITES(X)                                                                 \
    X("core",        runCoreTests,           "the axes, the clock, input, parallelFor")    \
    X("ecs",         runEcsTests,            "slot allocator and event bus")               \
    X("hierarchy",   runHierarchyTests,      "the entity graph everything else stands on") \
    X("query",       runPhysicsQueryTests,   "rays and sweeps: what they hit, where, and what they pass") \
    X("collision",   runPhysicsCollisionTests, "contacts, meshes, layers, pairs and triggers") \
    X("solver",      runPhysicsSolverTests,  "contacts carried, sleep and islands, the same world twice") \
    X("joint",       runPhysicsJointTests,   "joints: what they hold, and how stiffly") \
    X("ragdoll",     runPhysicsRagdollTests, "the ragdoll: built, posed, handed over, at rest") \
    X("character",   runPhysicsCharacterTests, "the character controller: steps, jumps, pushing") \
    X("resource",    runResourceTests,       "assets: handles, names, what outlives what") \
    X("async",       runAsyncTests,          "the async load queue: what lands, what is dropped") \
    X("cook",        runCookTests,           "the cooked-asset format and what a cook re-bakes") \
    X("import",      runImportTests,         "what a model file imports as: its unit, its V, its tangents") \
    X("authoring",   runAuthoringTests,      "probe dilation, gizmo snap, picking, the default layout's tabs") \
    X("prefab",      runPrefabTests,         "the prefab file: what it round-trips, what it refuses") \
    X("scene",       runSceneTests,          "scene")                                      \
    X("play",        runPlayTests,           "the editor play/stop snapshot, and who has the input") \
    X("undo",        runUndoTests,           "the editor's undo history")                  \
    X("culling",     runCullingTests,        "frustum extraction and the AABB test")       \
    X("sky",         runSkyTests,            "the sun: where the key light points, and its colour") \
    X("animation",   runAnimationTests,      "AnimationTrack sampling and its boundaries") \
    X("particle",    runParticleTests,       "emitter spawn accounting")                   \
    X("wire",        runNetWireTests,        "net wire")                                   \
    X("transport",   runNetTransportTests,   "net transport")                              \
    X("replication", runNetReplicationTests, "net replication")                            \
    X("prediction",  runNetPredictionTests,  "net prediction")                             \
    X("session",     runNetSessionTests,     "net session")                                \
    X("ui",          runUITests,             "UI layout, clipping, scrolling and text")    \
    X("audio",       runAudioTests,          "the mixer, measured off the signal it makes")\
    X("script",      runScriptTests,         "behaviors: the lifecycle and the two timelines") \
    X("hostile",     runHostileTests,        "every reader of outside bytes, fed damaged copies") \
    X("docs",        runDocsTests,           "the claims the tree makes about itself")

#define VKM_DECLARE_SUITE(name, entry, subject) void entry();
VKM_TEST_SUITES(VKM_DECLARE_SUITE)
#undef VKM_DECLARE_SUITE
