import QtQuick 2.15
import QtTest 1.15
import "../../qml/settings" as Settings

// Scope note: the keyboard's key buttons have no objectName/id exposed to
// external code (and adding one would be a src/ change), so real button taps
// can't be driven reliably by coordinate-based mouseClick without fragile
// pixel-position assumptions. This suite instead tests the public state
// machine directly (shiftOnce/capsLock/shiftActive derivation, row
// switching) and the signal contract (keyPressed/backspace/done) -- the same
// surface the real key handlers drive internally.
TestCase {
    name: "OnScreenKeyboard"

    Component {
        id: kbComp
        Settings.OnScreenKeyboard {}
    }

    function test_defaultsAreIdle() {
        const kb = createTemporaryObject(kbComp, null)
        compare(kb.shiftOnce, false)
        compare(kb.capsLock, false)
        compare(kb.shiftActive, false)
    }

    function test_shiftOnceMakesShiftActive() {
        const kb = createTemporaryObject(kbComp, null)
        kb.shiftOnce = true
        compare(kb.shiftActive, true)
    }

    function test_capsLockMakesShiftActiveIndependentlyOfShiftOnce() {
        const kb = createTemporaryObject(kbComp, null)
        kb.capsLock = true
        compare(kb.shiftOnce, false)
        compare(kb.shiftActive, true)
    }

    function test_symbolsTogglesActiveRows() {
        const kb = createTemporaryObject(kbComp, null)
        compare(kb.activeRows, kb.letterRows)
        kb.symbols = true
        compare(kb.activeRows, kb.symbolRows)
    }

    function test_signalsCarryExpectedArguments() {
        const kb = createTemporaryObject(kbComp, null)
        let lastKey = null
        let backspaceCount = 0
        let doneCount = 0
        kb.keyPressed.connect(function(ch) { lastKey = ch })
        kb.backspace.connect(function() { backspaceCount++ })
        kb.done.connect(function() { doneCount++ })

        kb.keyPressed("a")
        compare(lastKey, "a")

        kb.backspace()
        compare(backspaceCount, 1)

        kb.done()
        compare(doneCount, 1)
    }
}
