#pragma once

// One entry point per suite. Each is defined in the file named after it, so
// adding a suite is a file and a line here rather than an edit to a list
// somebody has to find.

void runPhysicsTests();   // physics
void runCoreTests();   // core
void runSceneTests();   // scene
void runPlayTests();   // the editor play/stop snapshot
void runResourceTests();   // assets: handles, names, and what outlives what
void runNetWireTests();   // net wire
void runNetTransportTests();   // net transport
void runNetReplicationTests();   // net replication
void runNetPredictionTests();   // net prediction
void runNetSessionTests();   // net session
void runEcsTests();   // ecs primitives: slots and the event bus
void runCullingTests();   // frustum extraction and the AABB test
void runAnimationTests();   // AnimationTrack sampling and its boundaries
void runParticleTests();   // emitter spawn accounting
void runDocsTests();   // what the manual points at
