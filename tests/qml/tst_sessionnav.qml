import QtQuick 2.15
import QtQuick.Controls.Basic
import QtTest 1.15
import "../../qml/settings/SessionNav.js" as SessionNav

// Exercises the real post-delete navigation used by SettingsDetail's delete
// flow against a live StackView, without loading the full Details page. The
// stack mirrors production depth: menu → tracks-list → track's-sessions →
// session-summary (top), so popAfterDelete's pop targets land on the same
// pages the user would see.
TestCase {
    id: testRoot
    name: "SessionNav"
    width: 200; height: 200
    visible: true

    // Stand-in for SessionModel — only hasSessionsForTrack matters to the
    // navigation decision. deleteSession's effect is covered by tst_sessionmodel.
    QtObject {
        id: mockModel
        property bool sessionsRemain: true
        function hasSessionsForTrack(trackId) { return sessionsRemain }
    }

    Component { id: pageComp; Item {} }
    Component { id: stackComp; StackView { anchors.fill: parent } }

    // Builds a 4-deep stack and returns { stack, menu, tracks, trackSessions, summary }.
    function buildStack() {
        const stack = createTemporaryObject(stackComp, testRoot)
        const menu          = stack.push(pageComp)  // depth 1 — settings menu
        const tracks        = stack.push(pageComp)  // depth 2 — Sessions (track groups) list
        const trackSessions = stack.push(pageComp)  // depth 3 — one track's sessions
        const summary       = stack.push(pageComp)  // depth 4 — session summary (delete lives here)
        return { stack, menu, tracks, trackSessions, summary }
    }

    function test_remainingSessionsPopOneStepBackToTrackList() {
        mockModel.sessionsRemain = true
        const s = buildStack()
        compare(s.stack.depth, 4)

        SessionNav.popAfterDelete(s.stack, mockModel, "spa")

        tryCompare(s.stack, "depth", 3)
        compare(s.stack.currentItem, s.trackSessions)
    }

    function test_noSessionsLeftPopToTracksList() {
        mockModel.sessionsRemain = false
        const s = buildStack()
        compare(s.stack.depth, 4)

        SessionNav.popAfterDelete(s.stack, mockModel, "spa")

        tryCompare(s.stack, "depth", 2)
        compare(s.stack.currentItem, s.tracks)
    }
}
