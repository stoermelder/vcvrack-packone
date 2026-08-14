// SpliceKit.scenes.test.cpp — scene management.
// Tests copyScene and requestCopyScene, MIDI scene activation (including the
// double-activation copy gesture), scene linking between instances, and
// reconcileScene.

#include "SpliceKit.test.hpp"


// copyScene

TEST_CASE("copyScene - no-op when src equals dst", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->sceneStore.setConnection(2, 0, 1, true);
	m->sceneStore.copy(2, 2);
	REQUIRE(m->sceneStore.isConnected(2, 0, 1) == true);  // unchanged
	Test::destroyModule(m);
}

TEST_CASE("copyScene - copies connections from src to dst", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->sceneStore.current = 7;  // keep src and dst both inactive
	m->sceneStore.setConnection(1, 0, 5, true);
	m->sceneStore.setConnection(1, 3, 7, true);
	m->sceneStore.copy(1, 4);
	REQUIRE(m->sceneStore.isConnected(4, 0, 5) == true);
	REQUIRE(m->sceneStore.isConnected(4, 3, 7) == true);
	REQUIRE(m->sceneStore.isConnected(4, 5, 0) == true);  // symmetric
	Test::destroyModule(m);
}

TEST_CASE("copyScene - overwrites existing connections in dst", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->sceneStore.current = 7;
	m->sceneStore.setConnection(0, 1, 2, true);  // src has 1↔2
	m->sceneStore.setConnection(3, 4, 5, true);  // dst has 4↔5 (will be overwritten)
	m->sceneStore.copy(0, 3);
	REQUIRE(m->sceneStore.isConnected(3, 1, 2) == true);
	REQUIRE(m->sceneStore.isConnected(3, 4, 5) == false);
	Test::destroyModule(m);
}

TEST_CASE("copyScene - src scene remains unchanged", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->sceneStore.current = 7;
	m->sceneStore.setConnection(2, 0, 3, true);
	m->sceneStore.copy(2, 5);
	REQUIRE(m->sceneStore.isConnected(2, 0, 3) == true);
	Test::destroyModule(m);
}

TEST_CASE("copyScene - unrelated scenes are not affected", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->sceneStore.current = 7;
	m->sceneStore.setConnection(1, 0, 5, true);
	m->sceneStore.setConnection(6, 10, 20, true);
	m->sceneStore.copy(1, 4);
	REQUIRE(m->sceneStore.isConnected(6, 10, 20) == true);
	REQUIRE(m->sceneStore.isConnected(4, 10, 20) == false);
	Test::destroyModule(m);
}

TEST_CASE("copyScene - can copy to current scene (bitmask transfer, no cables in test)", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	// src=1 is inactive, dst=4 is current. reconcileScene always assigns newConns.
	m->sceneStore.current = 4;
	m->sceneStore.setConnection(1, 0, 3, true);
	m->sceneStore.copy(1, 4);
	REQUIRE(m->sceneStore.isConnected(4, 0, 3) == true);
	Test::destroyModule(m);
}


// ---------------------------------------------------------------------------
// requestCopyScene
// ---------------------------------------------------------------------------

TEST_CASE("requestCopyScene - enqueues a taskProcessorUi item that performs the copy", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->sceneStore.current = 7;
	m->sceneStore.setConnection(2, 0, 1, true);
	REQUIRE(m->taskProcessorUi.internalQueue.queue.size() == 0);
	m->requestCopyScene(2, 5);
	REQUIRE(m->taskProcessorUi.internalQueue.queue.size() == 1);
	m->taskProcessorUi.internalQueue.queue.shift()();
	REQUIRE(m->sceneStore.isConnected(5, 0, 1) == true);
	Test::destroyModule(m);
}


// MIDI scene copy detection (processMapUpdate)

TEST_CASE("processMapUpdate - single scene activation sets pendingMidiSceneId and queues scene change", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	REQUIRE(m->pendingMidiSceneId == -1);
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 2, 100);
	REQUIRE(m->pendingMidiSceneId == 2);
	REQUIRE(m->taskProcessorUi.internalQueue.queue.size() == 1);  // switchScene enqueued
	Test::destroyModule(m);
}

TEST_CASE("processMapUpdate - MIDI scene activation is ignored while following a scene link master", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->sceneLinkMasterId = 999;  // any id — process()/processMapUpdate only check it's >= 0

	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 2, 100);

	REQUIRE(m->pendingMidiSceneId == -1);
	REQUIRE(m->taskProcessorUi.internalQueue.queue.size() == 0);
	Test::destroyModule(m);
}

TEST_CASE("processMapUpdate - note-off clears pendingMidiSceneId", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 3, 100);
	REQUIRE(m->pendingMidiSceneId == 3);
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 3, 0);
	REQUIRE(m->pendingMidiSceneId == -1);
	Test::destroyModule(m);
}

TEST_CASE("processMapUpdate - note-off for a different scene is a no-op", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->pendingMidiSceneId = 4;
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 7, 0);  // different scene
	REQUIRE(m->pendingMidiSceneId == 4);  // unchanged
	Test::destroyModule(m);
}

TEST_CASE("processMapUpdate - two activations without release queues scene copy", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	// First activation: normal scene change, pending set.
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 1, 100);
	REQUIRE(m->pendingMidiSceneId == 1);
	REQUIRE(m->taskProcessorUi.internalQueue.queue.size() == 1);

	// Second activation (different scene, no release): copy queued, pending cleared.
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 5, 100);
	REQUIRE(m->pendingMidiSceneId == -1);
	REQUIRE(m->taskProcessorUi.internalQueue.queue.size() == 2);  // switchScene(1) + copyScene(1, 5)

	Test::destroyModule(m);
}

TEST_CASE("processMapUpdate - same scene activated twice without release is not a copy", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 4, 100);
	REQUIRE(m->pendingMidiSceneId == 4);
	size_t queueSize = m->taskProcessorUi.internalQueue.queue.size();

	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 4, 100);  // same scene again
	REQUIRE(m->pendingMidiSceneId == 4);         // pending still set to same scene
	REQUIRE(m->taskProcessorUi.internalQueue.queue.size() == queueSize + 1);  // another switchScene, not copyScene

	Test::destroyModule(m);
}

TEST_CASE("processMapUpdate - after a copy, the next activation is treated normally", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	// Trigger a copy: press scene 1, then scene 2 (no release).
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 1, 100);
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 2, 100);
	REQUIRE(m->pendingMidiSceneId == -1);  // consumed by copy

	// Next activation should behave as a normal scene change.
	m->processMapUpdate(MidiTrackingType::NOTE, MATRIX_COUNT + 6, 100);
	REQUIRE(m->pendingMidiSceneId == 6);

	Test::destroyModule(m);
}


// Scene link — a follower re-syncs its currentScene from its configured master's
// currentScene, driven by notifyModuleListeners("SpliceKit-SceneLink") + process().

TEST_CASE("Scene link - follower adopts master's scene after a change", "[SpliceKit]") {
	SpliceKitModule* master = Test::createModule<SpliceKitModule>("SpliceKit");
	SpliceKitModule* follower = Test::createModule<SpliceKitModule>("SpliceKit");
	Test::registerModule(master);
	Test::registerModule(follower);

	follower->sceneLinkMasterId = master->id;
	master->sceneStore.switchTo(3);  // fires onSwitch -> notifyModuleListeners("SpliceKit-SceneLink")
	REQUIRE(follower->sceneStore.current == 0);  // not yet applied

	Test::SimpleEngine engine;
	engine.registerModule(follower);
	for (int i = 0; i < 256; i++) engine.step();  // let processDivider fire and drain moduleChangedFlag

	REQUIRE(follower->taskProcessorUi.internalQueue.queue.size() == 1);
	follower->taskProcessorUi.internalQueue.queue.shift()();
	REQUIRE(follower->sceneStore.current == 3);

	Test::unregisterModule(follower);
	Test::unregisterModule(master);
	Test::destroyModule(follower);
	Test::destroyModule(master);
}

TEST_CASE("Scene link - no-op when sceneLinkMasterId is unset", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	Test::registerModule(m);
	REQUIRE(m->sceneLinkMasterId == -1);

	m->moduleChangedFlag = true;
	Test::SimpleEngine engine;
	engine.registerModule(m);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(m->taskProcessorUi.internalQueue.queue.size() == 0);
	REQUIRE(m->sceneStore.current == 0);

	Test::unregisterModule(m);
	Test::destroyModule(m);
}

TEST_CASE("Scene link - unrelated instance without a configured master ignores the notification", "[SpliceKit]") {
	SpliceKitModule* master = Test::createModule<SpliceKitModule>("SpliceKit");
	SpliceKitModule* bystander = Test::createModule<SpliceKitModule>("SpliceKit");
	Test::registerModule(master);
	Test::registerModule(bystander);
	// bystander->sceneLinkMasterId stays -1

	master->sceneStore.switchTo(2);  // notifies every registered SpliceKit instance, including bystander

	Test::SimpleEngine engine;
	engine.registerModule(bystander);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(bystander->sceneStore.current == 0);
	REQUIRE(bystander->taskProcessorUi.internalQueue.queue.size() == 0);

	Test::unregisterModule(bystander);
	Test::unregisterModule(master);
	Test::destroyModule(bystander);
	Test::destroyModule(master);
}

TEST_CASE("Scene link - stale master reference is cleared once the master no longer exists", "[SpliceKit]") {
	SpliceKitModule* master = Test::createModule<SpliceKitModule>("SpliceKit");
	SpliceKitModule* follower = Test::createModule<SpliceKitModule>("SpliceKit");
	Test::registerModule(master);
	Test::registerModule(follower);
	follower->sceneLinkMasterId = master->id;

	Test::unregisterModule(master);
	Test::destroyModule(master);

	follower->moduleChangedFlag = true;  // simulate a pending notification arriving late
	Test::SimpleEngine engine;
	engine.registerModule(follower);
	for (int i = 0; i < 256; i++) engine.step();

	REQUIRE(follower->sceneLinkMasterId == -1);
	REQUIRE(follower->taskProcessorUi.internalQueue.queue.size() == 0);

	Test::unregisterModule(follower);
	Test::destroyModule(follower);
}

TEST_CASE("Scene link - sceneLinkCandidateIsFollower rejects chaining through an already-following module", "[SpliceKit]") {
	SpliceKitModule* a = Test::createModule<SpliceKitModule>("SpliceKit");
	SpliceKitModule* b = Test::createModule<SpliceKitModule>("SpliceKit");
	SpliceKitModule* c = Test::createModule<SpliceKitModule>("SpliceKit");
	Test::registerModule(a);
	Test::registerModule(b);
	Test::registerModule(c);

	// No links yet: any module is a valid pick.
	REQUIRE(SpliceKitModule::sceneLinkCandidateIsFollower(a->id) == false);
	REQUIRE(SpliceKitModule::sceneLinkCandidateIsFollower(b->id) == false);

	// b now follows a, so b is no longer a valid master for anyone (chains are disallowed).
	b->sceneLinkMasterId = a->id;
	REQUIRE(SpliceKitModule::sceneLinkCandidateIsFollower(b->id) == true);
	// a itself is still a valid pick (a follows nobody).
	REQUIRE(SpliceKitModule::sceneLinkCandidateIsFollower(a->id) == false);
	// c is unrelated and still a valid pick.
	REQUIRE(SpliceKitModule::sceneLinkCandidateIsFollower(c->id) == false);

	Test::unregisterModule(c);
	Test::unregisterModule(b);
	Test::unregisterModule(a);
	Test::destroyModule(c);
	Test::destroyModule(b);
	Test::destroyModule(a);
}

TEST_CASE("Scene link - sceneLinkHasFollowers detects when a module already serves as a master", "[SpliceKit]") {
	SpliceKitModule* a = Test::createModule<SpliceKitModule>("SpliceKit");
	SpliceKitModule* b = Test::createModule<SpliceKitModule>("SpliceKit");
	Test::registerModule(a);
	Test::registerModule(b);

	REQUIRE(a->sceneLinkHasFollowers() == false);
	REQUIRE(b->sceneLinkHasFollowers() == false);

	// b follows a, so a now has a follower and must not be allowed to pick its own master.
	b->sceneLinkMasterId = a->id;
	REQUIRE(a->sceneLinkHasFollowers() == true);
	REQUIRE(b->sceneLinkHasFollowers() == false);

	Test::unregisterModule(b);
	Test::unregisterModule(a);
	Test::destroyModule(b);
	Test::destroyModule(a);
}


// reconcileScene — non-current scene path

TEST_CASE("reconcileScene - non-current scene copies newConns without touching currentScene cables", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");

	m->sceneStore.current = 0;
	m->sceneStore.setConnection(0, 0, 1, true);  // current scene has a connection
	m->sceneStore.setConnection(2, 5, 7, true);  // scene 2 has its own connection (will be overwritten)

	SceneConns newConns{};
	newConns[2] = (1ULL << 3) | (1ULL << 9);  // 2↔3 and 2↔9
	m->sceneStore.reconcile(2, newConns);

	// Scene 0 (current) is unchanged
	REQUIRE(m->sceneStore.isConnected(0, 0, 1) == true);
	// Scene 2 reflects newConns
	REQUIRE(m->sceneStore.isConnected(2, 2, 3) == true);
	REQUIRE(m->sceneStore.isConnected(2, 2, 9) == true);
	// Old scene-2 connections are gone
	REQUIRE(m->sceneStore.isConnected(2, 5, 7) == false);
	Test::destroyModule(m);
}

TEST_CASE("reconcileScene - non-current scene with all-zero newConns clears that scene", "[SpliceKit]") {
	SpliceKitModule* m = Test::createModule<SpliceKitModule>("SpliceKit");
	m->sceneStore.current = 7;  // keep current scene inactive
	m->sceneStore.setConnection(3, 1, 2, true);
	REQUIRE(m->sceneStore.isConnected(3, 1, 2) == true);

	SceneConns empty{};
	m->sceneStore.reconcile(3, empty);
	REQUIRE(m->sceneStore.isConnected(3, 1, 2) == false);
	Test::destroyModule(m);
}
