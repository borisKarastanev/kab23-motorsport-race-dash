import QtQuick 2.15
import QtTest 1.15
import "../../qml" as App

// Wiring only — the pulse itself is AlertPulse's, covered via tst_gauge.
TestCase {
    name: "OilPressureWarning"

    Component {
        id: warningComp
        App.OilPressureWarning { width: 800; height: 480 }
    }

    function test_pulsesWhileShownAndResetsWhenHidden() {
        // Parentless, like tst_gauge: the TestCase item itself is never
        // visible, so a child of it would be hidden and never pulse.
        const w = createTemporaryObject(warningComp, null)
        verify(w !== null)
        tryVerify(() => w.pulseAlpha < 1.0, 1000)

        w.visible = false
        compare(w.pulseAlpha, 1.0)
    }
}
