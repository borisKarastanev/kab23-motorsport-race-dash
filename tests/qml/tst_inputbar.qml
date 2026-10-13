import QtQuick 2.15
import QtTest 1.15
import "../../qml/settings" as Settings

TestCase {
    name: "InputBar"
    when: windowShown

    Component {
        id: barComp
        Settings.InputBar { height: 304 } // track = 304 - labels - margins
    }

    // Fill height animates over 100 ms; wait for it to settle.
    function settledFill(bar) {
        wait(200)
        return bar.fillHeight
    }

    function test_fillIsProportional() {
        const bar = createTemporaryObject(barComp, this, { value: 0 })
        verify(bar !== null)
        compare(settledFill(bar), 0)

        bar.value = 100
        const full = settledFill(bar)
        verify(full > 0)

        bar.value = 25
        fuzzyCompare(settledFill(bar), full * 0.25, 0.5)
    }

    function test_valueClampedAbove100() {
        const bar = createTemporaryObject(barComp, this, { value: 100 })
        const full = settledFill(bar)
        bar.value = 150
        fuzzyCompare(settledFill(bar), full, 0.5)
        compare(bar.valueText, "100%")
    }

    function test_invalidShowsEmptyDash() {
        const bar = createTemporaryObject(barComp, this, { value: 80, valid: false })
        compare(settledFill(bar), 0)
        compare(bar.valueText, "--")
    }
}
