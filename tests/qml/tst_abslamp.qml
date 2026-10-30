import QtQuick 2.15
import QtTest 1.15
import "../../qml" as App

// Wiring only — the pulse itself is AlertPulse's, covered via tst_gauge.
TestCase {
    name: "AbsLamp"

    Component {
        id: lampComp
        App.AbsLamp {}
    }

    function test_hiddenWhenIdle() {
        const l = createTemporaryObject(lampComp, null)
        verify(l !== null)
        verify(!l.visible)
    }

    function test_pulsingLedsWhileActive() {
        // Parentless, like tst_oilpressurewarning: a child of the never-visible
        // TestCase item would be hidden and never pulse.
        const l = createTemporaryObject(lampComp, null, { active: true })
        verify(l.visible)
        const leds = findChild(l, "absLeds")
        verify(leds.visible)
        compare(l.ledCount, 3)
        compare(leds.children.length - 1, l.ledCount) // the LEDs plus the Repeater
        verify(!findChild(l, "absFaultBadge").visible)
        tryVerify(() => l.pulseAlpha < 1.0, 1000)

        l.active = false
        verify(!l.visible)
        compare(l.pulseAlpha, 1.0)
    }

    function test_faultIsSteadyRedBadgeAndWinsOverActive() {
        const l = createTemporaryObject(lampComp, null, { active: true, fault: true })
        verify(l.visible)
        const badge = findChild(l, "absFaultBadge")
        verify(badge.visible)
        compare(badge.border.color, Qt.color("#ff3333"))
        verify(!findChild(l, "absLeds").visible)
        wait(800) // longer than a full danger half-cycle
        compare(l.pulseAlpha, 1.0)
    }
}
