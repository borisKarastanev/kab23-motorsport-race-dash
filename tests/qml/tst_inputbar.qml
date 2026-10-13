import QtQuick 2.15
import QtTest 1.15
import "../../qml/settings" as Settings

TestCase {
    name: "InputBar"
    when: windowShown

    Component {
        id: barComp
        Settings.InputBar { height: 304 }
    }

    function fillHeight(bar) {
        return findChild(bar, "fill").height
    }

    function test_fillIsProportional() {
        const bar = createTemporaryObject(barComp, this, { value: 0 })
        verify(bar !== null)
        compare(fillHeight(bar), 0)

        bar.value = 100
        const full = fillHeight(bar)
        verify(full > 0)

        bar.value = 25
        fuzzyCompare(fillHeight(bar), full * 0.25, 0.5)
    }

    function test_valueClampedAbove100() {
        const bar = createTemporaryObject(barComp, this, { value: 100 })
        const full = fillHeight(bar)
        bar.value = 150
        compare(fillHeight(bar), full)
        compare(bar.valueText, "100%")
    }

    function test_invalidShowsEmptyDash() {
        const bar = createTemporaryObject(barComp, this, { value: 80, valid: false })
        compare(fillHeight(bar), 0)
        compare(bar.valueText, "--")
    }

    function test_binaryShowsOnOff() {
        const bar = createTemporaryObject(barComp, this, { value: 0, binary: true })
        compare(bar.valueText, "OFF")
        bar.value = 100
        compare(bar.valueText, "ON")
        bar.valid = false
        compare(bar.valueText, "--")
    }
}
