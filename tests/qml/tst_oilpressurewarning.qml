import QtQuick 2.15
import QtTest 1.15
import "../../qml" as App

// Wiring only — the pulse itself is AlertPulse's, covered via tst_gauge.
TestCase {
    name: "OilPressureWarning"
    when: windowShown

    Component {
        id: warningComp
        App.OilPressureWarning { width: 800; height: 480 }
    }

    function test_pulsesWhileShownAndResetsWhenHidden() {
        const w = createTemporaryObject(warningComp, this)
        verify(w !== null)
        tryVerify(() => w.pulseAlpha < 1.0, 1000)

        w.visible = false
        compare(w.pulseAlpha, 1.0)
    }
}
